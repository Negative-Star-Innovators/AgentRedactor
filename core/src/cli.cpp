// OS-agnostic CLI command implementation. See cli.h for the design notes.
#include "cli.h"
#include "utils.h"
#include "constants.h"

#include <cwctype>
#include <regex>
#include <sstream>
#include <iomanip>

using namespace AgentRedactor;

namespace {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

std::wstring WideLower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

std::wstring Trim(const std::wstring& s) {
    size_t b = s.find_first_not_of(L" \t\r\n");
    if (b == std::wstring::npos) return L"";
    size_t e = s.find_last_not_of(L" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::wstring JStr(const json& j, const char* key) {
    if (j.contains(key) && j.at(key).is_string()) return Utils::Utf8ToWide(j.at(key).get<std::string>());
    return L"";
}

std::wstring BoolStr(bool b) { return b ? L"true" : L"false"; }

bool ParseBool(const std::wstring& s, bool& out) {
    std::wstring v = WideLower(Trim(s));
    if (v == L"true" || v == L"1" || v == L"on" || v == L"yes") { out = true; return true; }
    if (v == L"false" || v == L"0" || v == L"off" || v == L"no") { out = false; return true; }
    return false;
}

bool IsInteger(const std::wstring& s) {
    if (s.empty()) return false;
    for (wchar_t c : s) if (!std::iswdigit(c)) return false;
    return true;
}

std::wstring FormatFloat(float f) {
    std::wostringstream oss;
    oss << f;
    return oss.str();
}

// Mirrors the GUI's upstream URL validation: non-empty, http(s) scheme, and a
// real host. Returns the error message (labelled for the input's name), or an
// empty string when valid.
std::wstring ValidateUpstreamUrl(const std::wstring& raw, const std::wstring& label) {
    std::wstring url = Trim(raw);
    if (url.empty()) return label + L" must not be empty";
    std::wstring lowerUrl = WideLower(url);
    if (!Utils::StartsWith(lowerUrl, L"http://") && !Utils::StartsWith(lowerUrl, L"https://")) {
        return label + L" must start with http:// or https://";
    }
    size_t schemeEnd = lowerUrl.find(L"://");
    size_t hostStart = schemeEnd + 3;
    size_t hostEnd = lowerUrl.find_first_of(L"/?", hostStart);
    if (hostEnd == std::wstring::npos) hostEnd = lowerUrl.size();
    if (lowerUrl.substr(hostStart, hostEnd - hostStart).empty()) {
        return label + L" has no host";
    }
    return L"";
}

// ---------------------------------------------------------------------------
// Invocation context
// ---------------------------------------------------------------------------

struct CliOptions {
    std::vector<std::wstring> positional;
    std::optional<std::wstring> profile;
    std::optional<std::wstring> port;        // profiles add --port
    std::optional<std::wstring> upstreamUrl; // profiles add --upstream-url
    std::optional<std::wstring> apiKey;      // profiles add --api-key
    bool ignoreCase = false;
};

bool ParseOptions(const std::vector<std::wstring>& args, CliOptions& opts, std::wstring& error) {
    for (size_t i = 0; i < args.size(); ++i) {
        const std::wstring& a = args[i];
        auto flagged = [&](const std::wstring& name, std::optional<std::wstring>& out) -> int {
            // 0 = not this flag, 1 = consumed, -1 = error
            if (a == name) {
                if (i + 1 >= args.size()) { error = name + L" requires a value"; return -1; }
                out = args[++i];
                return 1;
            }
            if (a.size() > name.size() + 1 && a.compare(0, name.size() + 1, name + L"=") == 0) {
                out = a.substr(name.size() + 1);
                return 1;
            }
            return 0;
        };
        int r = flagged(L"--profile", opts.profile);
        if (r < 0) return false;
        if (r > 0) continue;
        r = flagged(L"--port", opts.port);
        if (r < 0) return false;
        if (r > 0) continue;
        r = flagged(L"--upstream-url", opts.upstreamUrl);
        if (r < 0) return false;
        if (r > 0) continue;
        r = flagged(L"--api-key", opts.apiKey);
        if (r < 0) return false;
        if (r > 0) continue;
        if (a == L"--ignore-case") { opts.ignoreCase = true; continue; }
        if (!a.empty() && a[0] == L'-' && a != L"-") {
            error = L"unknown option: " + a;
            return false;
        }
        opts.positional.push_back(a);
    }
    return true;
}

struct Ctx {
    const CliTransport& t;
    const CliConsole& c;
    CliOptions opts;

    void Print(const std::wstring& line) const { c.print(line); }
    void Error(const std::wstring& msg) const { c.print(L"error: " + msg); }

    // Fetches /status; false means the engine is unreachable.
    bool EngineStatus(json& status) const {
        if (t.get(L"/status", status)) return true;
        Error(L"engine is not running (launch the Agent Redactor app to start it)");
        return false;
    }

    // Protection gate for every settings/profile command. Open when no
    // protection is enabled. With Windows Hello enabled, EVERY gated command
    // demands a fresh consent prompt right there (there is no `unlock`
    // command anymore). The consent runs IN-PROCESS via the transport (the
    // client is the active application, so the Windows dialog comes to the
    // foreground) and on success unlocks the engine session (POST /unlock),
    // exactly like the GUI after its in-process prompt. `status`/`help` stay
    // open so the CLI remains introspectable.
    bool EnsureConsent(const json& status) const {
        if (!status.value("masterPasswordEnabled", false)) return true;
        if (!status.value("helloEnabled", false)) {
            Error(L"windows hello is not configured on this device");
            return false;
        }
        if (!t.consent) {
            Error(L"windows hello is not available on this device");
            return false;
        }
        switch (t.consent()) {
        case HelloConsentOutcome::Granted:
            return true;
        case HelloConsentOutcome::Canceled:
            Error(L"windows hello consent canceled");
            return false;
        case HelloConsentOutcome::RetriesExhausted:
            Error(L"too many windows hello attempts");
            return false;
        case HelloConsentOutcome::TimedOut:
            Error(L"windows hello consent timed out (no answer)");
            return false;
        case HelloConsentOutcome::Unavailable:
            Error(L"windows hello is not available on this device");
            return false;
        default:
            Error(L"windows hello consent required");
            return false;
        }
    }

    // Combined precondition used by all gated commands.
    bool Gate(json& status) const {
        if (!EngineStatus(status)) return false;
        return EnsureConsent(status);
    }

    // Resolves --profile against GET /profiles. Without --profile a single
    // configured profile is implied. On success `profile` holds the full
    // profile JSON (api_key masked — it round-trips as "unchanged" on PUT).
    bool SelectProfile(json& profile) const {
        json profiles;
        if (!t.get(L"/profiles", profiles) || !profiles.is_array()) {
            Error(L"could not read profiles from the engine");
            return false;
        }
        if (profiles.empty()) {
            Error(L"no profiles configured (create one in the GUI first)");
            return false;
        }
        if (!opts.profile) {
            if (profiles.size() == 1) { profile = profiles[0]; return true; }
            Error(L"multiple profiles configured; pass --profile <n|id|alias>");
            return false;
        }
        const std::wstring sel = *opts.profile;
        if (IsInteger(sel)) {
            size_t n = 0;
            try { n = static_cast<size_t>(std::stoul(sel)); } catch (...) { n = 0; }
            if (n >= 1 && n <= profiles.size()) { profile = profiles[n - 1]; return true; }
            Error(L"profile number out of range: " + sel);
            return false;
        }
        const std::wstring selLower = WideLower(sel);
        for (const auto& p : profiles) {
            if (WideLower(JStr(p, "id")) == selLower || WideLower(JStr(p, "alias")) == selLower) {
                profile = p;
                return true;
            }
        }
        Error(L"profile not found: " + sel);
        return false;
    }

    bool PutProfile(const json& profile) const {
        json out;
        if (t.put(L"/profiles/" + JStr(profile, "id"), profile, &out)) return true;
        Error(L"failed to update the profile");
        return false;
    }
};

// ---------------------------------------------------------------------------
// status / password / languages
// ---------------------------------------------------------------------------

int CmdStatus(const Ctx& ctx) {
    json status;
    if (!ctx.EngineStatus(status)) return 1;

    ctx.Print(L"engine:          " + Utils::Utf8ToWide(status.value("engineVersion", std::string("?"))));
    ctx.Print(L"password enabled: " + BoolStr(status.value("helloEnabled", false)));
    if (status.value("modelDownloadInProgress", false)) {
        ctx.Print(L"model download:  in progress (" + std::to_wstring(status.value("modelDownloadPercent", 0)) + L"%)");
    } else if (status.value("modelDownloadFailed", false)) {
        ctx.Print(L"model download:  failed - " + Utils::Utf8ToWide(status.value("modelDownloadStatus", std::string(""))));
    } else if (status.value("modelDownloadRequired", false)) {
        ctx.Print(L"model download:  required");
    }

    // /status profiles carry only id/port/running; merge alias + stats from
    // GET /profiles when available (ungated endpoint, may still fail offline).
    json profiles;
    const bool haveProfiles = ctx.t.get(L"/profiles", profiles) && profiles.is_array();
    if (status.contains("profiles") && status["profiles"].is_array()) {
        ctx.Print(L"profiles:");
        int i = 0;
        for (const auto& sp : status["profiles"]) {
            ++i;
            const std::wstring id = JStr(sp, "id");
            std::wstring alias;
            uint64_t requests = 0, redactions = 0;
            if (haveProfiles) {
                for (const auto& p : profiles) {
                    if (JStr(p, "id") == id) {
                        alias = JStr(p, "alias");
                        if (p.contains("stats")) {
                            const auto& st = p["stats"];
                            requests = st.value("total_requests", 0ULL);
                            redactions = st.value("total_pii_detected", 0ULL)
                                       + st.value("total_regex_matches", 0ULL)
                                       + st.value("total_keyword_matches", 0ULL);
                        }
                        break;
                    }
                }
            }
            std::wstring line = L"  " + std::to_wstring(i) + L"  " + (alias.empty() ? id : alias)
                + L"  port " + std::to_wstring(sp.value("port", 0))
                + (sp.value("proxyRunning", false) ? L"  proxy running" : L"  proxy stopped")
                + L"  requests=" + std::to_wstring(requests)
                + L"  redactions=" + std::to_wstring(redactions);
            ctx.Print(line);
        }
    }
    return 0;
}

int CmdLanguages(const Ctx& ctx) {
    // One code per line, nothing else: the same list the Settings page and
    // tray menu are built from (core/include/constants.h SUPPORTED_LANGUAGES).
    for (const auto& lang : ::AgentRedactor::SUPPORTED_LANGUAGES) {
        ctx.Print(lang.tag);
    }
    return 0;
}

int CmdPassword(const Ctx& ctx) {
    if (ctx.opts.positional.size() < 2) {
        ctx.Error(L"usage: agentredactor password enable | disable");
        return 2;
    }
    const std::wstring action = WideLower(ctx.opts.positional[1]);
    json status;
    if (!ctx.EngineStatus(status)) return 1;

    if (action == L"enable") {
        if (status.value("masterPasswordEnabled", false)) {
            ctx.Error(L"windows hello protection is already enabled");
            return 1;
        }
        // Windows-Hello-only protection: no typed password exists. The GUI
        // asks for the consent prompt before enabling; the CLI (a headless
        // tool) enables directly — the locked session still demands the Hello
        // prompt before anything sensitive is readable.
        if (!ctx.t.put(L"/settings/enableMasterPassword", json{{"value", ""}, {"hello", true}}, nullptr)) {
            ctx.Error(L"failed to enable windows hello protection");
            return 1;
        }
        ctx.Print(L"windows hello protection enabled");
        return 0;
    }

    if (action == L"disable") {
        if (!status.value("masterPasswordEnabled", false)) {
            ctx.Print(L"windows hello protection is not enabled");
            return 0;
        }
        // Disabling strips ALL protection, so it demands the same fresh
        // Windows Hello consent as every other gated command; the engine's
        // disable endpoint only accepts an unlocked session, and the consent
        // above unlocked it. A raw API caller cannot disable without either a
        // Hello consent or the trusted /unlock path.
        if (!ctx.EnsureConsent(status)) return 1;
        json out;
        if (!ctx.t.put(L"/settings/disableMasterPassword", json{{"value", true}}, &out) || !out.value("ok", false)) {
            ctx.Error(L"failed to disable windows hello protection");
            return 1;
        }
        ctx.Print(L"windows hello protection disabled");
        return 0;
    }

    ctx.Error(L"unknown password action: " + action + L" (enable|disable)");
    return 2;
}

// ---------------------------------------------------------------------------
// get / set
// ---------------------------------------------------------------------------

struct GlobalKey {
    const wchar_t* cliName;
    const char* apiName;
    char type; // 'b' bool, 's' string, 'r' read-only bool
};

constexpr GlobalKey kGlobalKeys[] = {
    {L"start-on-boot", "startOnBoot", 'b'},
    {L"logging", "loggingEnabled", 'b'},
    {L"show-sensitive", "showSensitive", 'b'},
    {L"app-language", "appLanguage", 's'},
};

struct ProfileKey {
    const wchar_t* cliName;
    const char* jsonName;
    char type; // 'b' bool, 's' string, 'i' int, 'f' float
};

constexpr ProfileKey kProfileKeys[] = {
    {L"alias", "alias", 's'},
    {L"upstream-url", "upstream_url", 's'},
    {L"port", "port", 'i'},
    {L"confidence-threshold", "pii_confidence_threshold", 'f'},
    {L"use-ai-model", "use_openai_model", 'b'},
};

const GlobalKey* FindGlobalKey(const std::wstring& name) {
    for (const auto& k : kGlobalKeys) if (name == k.cliName) return &k;
    return nullptr;
}

const ProfileKey* FindProfileKey(const std::wstring& name) {
    for (const auto& k : kProfileKeys) if (name == k.cliName) return &k;
    return nullptr;
}

void PrintValidKeys(const Ctx& ctx) {
    ctx.Print(L"global keys:  start-on-boot, logging, show-sensitive, app-language,");
    ctx.Print(L"profile keys: alias, upstream-url, api-key, port,");
    ctx.Print(L"              confidence-threshold, use-ai-model");
}

int CmdGet(const Ctx& ctx) {
    if (ctx.opts.positional.size() < 2) {
        ctx.Error(L"usage: agentredactor get <key> [--profile <n|id|alias>]");
        return 2;
    }
    const std::wstring key = WideLower(ctx.opts.positional[1]);

    // Every read is gated when a master password is set (the engine lock is
    // UX-level, mirroring the GUI); only status and help stay open.
    json status;
    if (!ctx.Gate(status)) return 1;

    if (key == L"api-key") {
        // Gate() above already demanded a fresh Windows Hello consent on
        // protected sessions (verifying when unlocked, unlocking + verifying
        // when locked); the engine's apikey endpoint then serves the key.
        json profile;
        if (!ctx.SelectProfile(profile)) return 1;
        json out;
        if (!ctx.t.get(L"/profiles/" + JStr(profile, "id") + L"/apikey", out)) {
            ctx.Error(L"failed to read the API key");
            return 1;
        }
        ctx.Print(Utils::Utf8ToWide(out.value("apiKey", std::string(""))));
        return 0;
    }

    if (const GlobalKey* gk = FindGlobalKey(key)) {
        json settings;
        if (!ctx.t.get(L"/settings", settings)) {
            ctx.Error(L"engine is not running (launch the Agent Redactor app to start it)");
            return 1;
        }
        const json& v = settings[gk->apiName];
        if (v.is_boolean()) ctx.Print(BoolStr(v.get<bool>()));
        else if (v.is_string()) ctx.Print(Utils::Utf8ToWide(v.get<std::string>()));
        else ctx.Print(Utils::Utf8ToWide(v.dump()));
        return 0;
    }

    if (const ProfileKey* pk = FindProfileKey(key)) {
        json profile;
        if (!ctx.SelectProfile(profile)) return 1;
        const json& v = profile[pk->jsonName];
        switch (pk->type) {
        case 'b': ctx.Print(BoolStr(v.is_boolean() && v.get<bool>())); return 0;
        case 'i': ctx.Print(std::to_wstring(v.get<int>())); return 0;
        case 'f': ctx.Print(FormatFloat(v.get<float>())); return 0;
        default: ctx.Print(JStr(profile, pk->jsonName)); return 0;
        }
    }

    ctx.Error(L"unknown key: " + key);
    PrintValidKeys(ctx);
    return 2;
}

int CmdSet(const Ctx& ctx) {
    if (ctx.opts.positional.size() < 3) {
        ctx.Error(L"usage: agentredactor set <key> <value> [--profile <n|id|alias>]");
        return 2;
    }
    const std::wstring key = WideLower(ctx.opts.positional[1]);
    const std::wstring& value = ctx.opts.positional[2];

    json status;
    if (!ctx.Gate(status)) return 1;

    if (const GlobalKey* gk = FindGlobalKey(key)) {
        json body;
        if (gk->type == 'b') {
            bool b;
            if (!ParseBool(value, b)) { ctx.Error(L"expected a boolean (true/false), got: " + value); return 2; }
            body["value"] = b;
        } else {
            if (key == L"app-language") {
                // Only exact supported BCP-47 tags work: MRT matches exact
                // candidates, so a partial tag like "zh" selects nothing
                // (silently staying on the OS language) and an unknown tag is
                // never applied. Canonicalize the case from the supported list.
                bool found = false;
                const std::wstring want = WideLower(value);
                for (const auto& lang : ::AgentRedactor::SUPPORTED_LANGUAGES) {
                    if (WideLower(lang.tag) == want) {
                        body["value"] = Utils::WideToUtf8(lang.tag);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    ctx.Error(L"unsupported language: " + value
                        + L" (run 'agentredactor languages' for the supported codes)");
                    return 2;
                }
            } else {
                body["value"] = Utils::WideToUtf8(value);
            }
        }
        if (!ctx.t.put(L"/settings/" + Utils::Utf8ToWide(gk->apiName), body, nullptr)) {
            ctx.Error(L"failed to update setting: " + key);
            return 1;
        }
        ctx.Print(L"ok");
        return 0;
    }

    json profile;
    if (key == L"api-key") {
        if (!ctx.SelectProfile(profile)) return 1;
        profile["api_key"] = Utils::WideToUtf8(value);
        if (!ctx.PutProfile(profile)) return 1;
        ctx.Print(L"ok");
        return 0;
    }

    const ProfileKey* pk = FindProfileKey(key);
    if (!pk) {
        ctx.Error(L"unknown key: " + key);
        PrintValidKeys(ctx);
        return 2;
    }
    if (!ctx.SelectProfile(profile)) return 1;

    // Mirror the GUI's upstream URL validation (non-empty, http/https, and a
    // real host). Kept OS-agnostic (no WinHTTP) but covers the same inputs.
    if (key == L"upstream-url") {
        std::wstring urlErr = ValidateUpstreamUrl(value, L"upstream-url");
        if (!urlErr.empty()) { ctx.Error(urlErr); return 2; }
        profile["upstream_url"] = Utils::WideToUtf8(Trim(value));
        if (!ctx.PutProfile(profile)) return 1;
        ctx.Print(L"ok");
        return 0;
    }

    switch (pk->type) {
    case 'b': {
        bool b;
        if (!ParseBool(value, b)) { ctx.Error(L"expected a boolean (true/false), got: " + value); return 2; }
        profile[pk->jsonName] = b;
        break;
    }
    case 'i': {
        if (!IsInteger(value)) { ctx.Error(L"expected an integer, got: " + value); return 2; }
        int port = 0;
        try { port = std::stoi(value); }
        catch (...) { ctx.Error(L"port out of range: " + value); return 2; }
        if (port < 1024 || port > 65535) {
            ctx.Error(L"port must be between 1024 and 65535");
            return 2;
        }
        // Mirror the GUI: a proxy port must not be shared with another profile.
        std::wstring myId = JStr(profile, "id");
        json allProfiles;
        if (ctx.t.get(L"/profiles", allProfiles) && allProfiles.is_array()) {
            for (const auto& o : allProfiles) {
                if (o.contains("port") && o.value("port", 0) == port && JStr(o, "id") != myId) {
                    ctx.Error(L"port " + value + L" is already used by profile '" + JStr(o, "alias") + L"'");
                    return 2;
                }
            }
        }
        profile[pk->jsonName] = port;
        break;
    }
    case 'f': {
        float f;
        try { f = std::stof(value); } catch (...) { ctx.Error(L"expected a number, got: " + value); return 2; }
        if (f < 0.0f || f > 1.0f) { ctx.Error(L"confidence-threshold must be between 0 and 1"); return 2; }
        profile[pk->jsonName] = f;
        break;
    }
    default:
        profile[pk->jsonName] = Utils::WideToUtf8(value);
        break;
    }
    if (!ctx.PutProfile(profile)) return 1;
    ctx.Print(L"ok");
    return 0;
}

// ---------------------------------------------------------------------------
// profiles list
// ---------------------------------------------------------------------------

int CmdProfilesList(const Ctx& ctx);
int CmdProfilesAdd(const Ctx& ctx);
int CmdProfilesDelete(const Ctx& ctx);

int CmdProfiles(const Ctx& ctx) {
    if (ctx.opts.positional.size() < 2) {
        ctx.Error(L"usage: agentredactor profiles list|add <alias>|delete <id>");
        return 2;
    }
    const std::wstring action = WideLower(ctx.opts.positional[1]);
    if (action == L"list") return CmdProfilesList(ctx);
    if (action == L"add") return CmdProfilesAdd(ctx);
    if (action == L"delete") return CmdProfilesDelete(ctx);
    ctx.Error(L"unknown profiles action: " + action + L" (list|add|delete)");
    return 2;
}

int CmdProfilesList(const Ctx& ctx) {
    json status;
    if (!ctx.Gate(status)) return 1;
    json profiles;
    if (!ctx.t.get(L"/profiles", profiles) || !profiles.is_array()) {
        ctx.Error(L"could not read profiles from the engine");
        return 1;
    }
    if (profiles.empty()) {
        ctx.Print(L"no profiles configured");
        return 0;
    }

    std::vector<std::vector<std::wstring>> rows;
    rows.push_back({L"#", L"alias", L"id", L"port", L"running", L"requests", L"redactions"});
    int i = 0;
    for (const auto& p : profiles) {
        uint64_t requests = 0, redactions = 0;
        if (p.contains("stats")) {
            const auto& st = p["stats"];
            requests = st.value("total_requests", 0ULL);
            redactions = st.value("total_pii_detected", 0ULL)
                       + st.value("total_regex_matches", 0ULL)
                       + st.value("total_keyword_matches", 0ULL);
        }
        rows.push_back({
            std::to_wstring(++i),
            JStr(p, "alias"),
            JStr(p, "id"),
            std::to_wstring(p.value("port", 0)),
            BoolStr(p.value("proxyRunning", false)),
            std::to_wstring(requests),
            std::to_wstring(redactions),
        });
    }
    std::vector<size_t> widths(rows[0].size(), 0);
    for (const auto& row : rows)
        for (size_t c = 0; c < row.size(); ++c)
            widths[c] = std::max(widths[c], row[c].size());
    for (const auto& row : rows) {
        std::wstring line;
        for (size_t c = 0; c < row.size(); ++c) {
            if (c) line += L"  ";
            line += row[c];
            if (c + 1 < row.size()) line += std::wstring(widths[c] - row[c].size(), L' ');
        }
        ctx.Print(line);
    }
    return 0;
}

int CmdProfilesAdd(const Ctx& ctx) {
    if (ctx.opts.positional.size() < 3) {
        ctx.Error(L"usage: agentredactor profiles add <alias> [--port N] [--upstream-url U] [--api-key K]");
        return 2;
    }
    const std::wstring alias = Trim(ctx.opts.positional[2]);
    if (alias.empty()) {
        ctx.Error(L"profile alias must not be empty");
        return 2;
    }

    json status;
    if (!ctx.Gate(status)) return 1;
    json profiles;
    if (!ctx.t.get(L"/profiles", profiles) || !profiles.is_array()) {
        ctx.Error(L"could not read profiles from the engine");
        return 1;
    }

    // Port: explicit --port (validated, incl. the GUI's "used by another
    // profile" check) or the first free port from 8080, like the GUI.
    int port = 0;
    if (ctx.opts.port) {
        const std::wstring v = *ctx.opts.port;
        if (!IsInteger(v)) { ctx.Error(L"expected an integer for --port, got: " + v); return 2; }
        try { port = std::stoi(v); }
        catch (...) { ctx.Error(L"port out of range: " + v); return 2; }
        if (port < 1024 || port > 65535) { ctx.Error(L"port must be between 1024 and 65535"); return 2; }
        for (const auto& o : profiles) {
            if (o.value("port", 0) == port) {
                ctx.Error(L"port " + v + L" is already used by profile '" + JStr(o, "alias") + L"'");
                return 2;
            }
        }
    } else {
        for (int candidate = 8080; candidate <= 65535; ++candidate) {
            bool used = false;
            for (const auto& o : profiles) {
                if (o.value("port", 0) == candidate) { used = true; break; }
            }
            if (!used) { port = candidate; break; }
        }
        if (port == 0) {
            ctx.Error(L"no free proxy port available");
            return 1;
        }
    }

    json body{
        {"alias", Utils::WideToUtf8(alias)},
        {"port", port},
    };
    if (ctx.opts.upstreamUrl) {
        std::wstring urlErr = ValidateUpstreamUrl(*ctx.opts.upstreamUrl, L"--upstream-url");
        if (!urlErr.empty()) { ctx.Error(urlErr); return 2; }
        body["upstream_url"] = Utils::WideToUtf8(Trim(*ctx.opts.upstreamUrl));
    }
    if (ctx.opts.apiKey) {
        body["api_key"] = Utils::WideToUtf8(*ctx.opts.apiKey);
    }
    json out;
    if (!ctx.t.post(L"/profiles", body, &out) || !out.value("ok", false)) {
        ctx.Error(L"failed to create the profile");
        return 1;
    }
    ctx.Print(L"created profile " + Utils::Utf8ToWide(out.value("id", std::string("")))
        + L" (" + alias + L")");
    return 0;
}

int CmdProfilesDelete(const Ctx& ctx) {
    if (ctx.opts.positional.size() < 3) {
        ctx.Error(L"usage: agentredactor profiles delete <id>");
        return 2;
    }
    json status;
    if (!ctx.Gate(status)) return 1;
    json profiles;
    if (!ctx.t.get(L"/profiles", profiles) || !profiles.is_array()) {
        ctx.Error(L"could not read profiles from the engine");
        return 1;
    }
    if (profiles.empty()) {
        ctx.Error(L"no profiles configured");
        return 1;
    }
    // Deletion is deliberately strict: only the profile id is accepted (an
    // alias or list number could point at a different profile later).
    const std::wstring sel = ctx.opts.positional[2];
    const std::wstring selLower = WideLower(sel);
    json target;
    bool found = false;
    for (const auto& p : profiles) {
        if (WideLower(JStr(p, "id")) == selLower) {
            target = p;
            found = true;
            break;
        }
    }
    if (!found) {
        ctx.Error(L"profile not found: " + sel);
        return 1;
    }
    if (profiles.size() <= 1) {
        ctx.Error(L"cannot delete the last profile");
        return 2;
    }
    if (!ctx.t.del(L"/profiles/" + JStr(target, "id"))) {
        ctx.Error(L"failed to delete the profile");
        return 1;
    }
    ctx.Print(L"deleted profile " + JStr(target, "id") + L" (" + JStr(target, "alias") + L")");
    return 0;
}

// ---------------------------------------------------------------------------
// pii-types list / enable / disable (per profile, single PII type only)
// ---------------------------------------------------------------------------

// Resolves a PII type id (e.g. secret, private_email) to a one-element list.
// Empty = unknown. The GUI only deals with individual types (no categories),
// so the CLI does too.
std::vector<std::wstring> ResolvePiiTypes(const std::wstring& sel) {
    const std::wstring s = WideLower(sel);
    for (const auto& t : DEFAULT_PII_TYPES) {
        if (WideLower(t) == s) return {t};
    }
    return {};
}

std::vector<std::wstring> EnabledPiiTypes(const json& profile) {
    std::vector<std::wstring> out;
    if (profile.contains("enabled_pii_types") && profile["enabled_pii_types"].is_array()) {
        for (const auto& v : profile["enabled_pii_types"]) {
            if (v.is_string()) out.push_back(Utils::Utf8ToWide(v.get<std::string>()));
        }
    }
    return out;
}

bool HasPiiType(const std::vector<std::wstring>& list, const std::wstring& type) {
    const std::wstring t = WideLower(type);
    for (const auto& e : list) if (WideLower(e) == t) return true;
    return false;
}

int CmdPiiTypes(const Ctx& ctx) {
    if (ctx.opts.positional.size() < 2) {
        ctx.Error(L"usage: agentredactor pii-types list|enable|disable <type> [--profile P]");
        return 2;
    }
    const std::wstring action = WideLower(ctx.opts.positional[1]);
    if (action != L"list" && action != L"enable" && action != L"disable") {
        ctx.Error(L"unknown pii-types action: " + action + L" (list|enable|disable)");
        return 2;
    }

    json status;
    if (!ctx.Gate(status)) return 1;
    json profile;
    if (!ctx.SelectProfile(profile)) return 1;
    std::vector<std::wstring> enabled = EnabledPiiTypes(profile);

    if (action == L"list") {
        if (enabled.empty()) {
            ctx.Print(L"(all PII types disabled for this profile)");
        }
        // Types not in our known list are listed as "other".
        std::vector<std::wstring> shown;
        for (const auto& t : DEFAULT_PII_TYPES) {
            ctx.Print(std::wstring(HasPiiType(enabled, t) ? L"[x] " : L"[ ] ") + t);
            shown.push_back(t);
        }
        for (const auto& t : enabled) {
            if (std::find(shown.begin(), shown.end(), t) == shown.end()) {
                ctx.Print(L"  OTHER: [x] " + t);
                shown.push_back(t);
            }
        }
        return 0;
    }

    const bool wantEnabled = (action == L"enable");
    const std::wstring sel = ctx.opts.positional.size() >= 3 ? ctx.opts.positional[2] : L"";
    if (sel.empty()) {
        ctx.Error(L"pii-types " + action + L" requires a PII type name");
        return 2;
    }
    std::vector<std::wstring> targets = ResolvePiiTypes(sel);
    if (targets.empty()) {
        ctx.Error(L"unknown PII type: " + sel);
        return 2;
    }

    bool changed = false;
    for (const auto& t : targets) {
        bool on = HasPiiType(enabled, t);
        if (wantEnabled && !on) {
            enabled.push_back(t);
            changed = true;
        } else if (!wantEnabled && on) {
            enabled.erase(std::remove_if(enabled.begin(), enabled.end(),
                [&t](const std::wstring& e) { return WideLower(e) == WideLower(t); }), enabled.end());
            changed = true;
        }
    }
    if (changed) {
        json arr = json::array();
        for (const auto& t : enabled) arr.push_back(Utils::WideToUtf8(t));
        profile["enabled_pii_types"] = arr;
        if (!ctx.PutProfile(profile)) return 1;
    }
    ctx.Print(L"ok");
    return 0;
}

// ---------------------------------------------------------------------------
// regex / keywords
// ---------------------------------------------------------------------------

// Shared list-mutation flow for regex_patterns and keywords: fetch profile,
// apply `mutate` to the entry array, PUT the profile back.
int MutateList(const Ctx& ctx, const char* field,
               const std::function<bool(json& entries)>& mutate) {
    json status;
    if (!ctx.Gate(status)) return 1;
    json profile;
    if (!ctx.SelectProfile(profile)) return 1;
    if (!profile.contains(field) || !profile[field].is_array()) profile[field] = json::array();
    if (!mutate(profile[field])) return 2;
    if (!ctx.PutProfile(profile)) return 1;
    return 0;
}

// Resolves a remove selector (1-based list index or exact text match).
bool RemoveEntry(const Ctx& ctx, json& entries, const std::wstring& sel, const char* textField) {
    size_t index = 0;
    if (IsInteger(sel)) {
        const size_t n = static_cast<size_t>(std::stoul(sel));
        if (n < 1 || n > entries.size()) {
            ctx.Error(L"list index out of range: " + sel);
            return false;
        }
        index = n - 1;
    } else {
        bool found = false;
        for (size_t k = 0; k < entries.size(); ++k) {
            if (JStr(entries[k], textField) == sel) { index = k; found = true; break; }
        }
        if (!found) {
            ctx.Error(L"no such entry: " + sel);
            return false;
        }
    }
    entries.erase(entries.begin() + static_cast<ptrdiff_t>(index));
    ctx.Print(L"ok");
    return true;
}

void PrintEntries(const Ctx& ctx, const json& entries, const char* textField, bool keywords) {
    if (entries.empty()) {
        ctx.Print(L"(none)");
        return;
    }
    int i = 0;
    for (const auto& e : entries) {
        std::wstring line = std::to_wstring(++i) + L"  "
            + (e.value("enabled", true) ? L"[x] " : L"[ ] ")
            + JStr(e, textField);
        if (keywords) line += std::wstring(e.value("case_sensitive", true) ? L"  (case-sensitive)" : L"  (ignore case)");
        ctx.Print(line);
    }
}

int CmdRegex(const Ctx& ctx) {
    if (ctx.opts.positional.size() < 2) {
        ctx.Error(L"usage: agentredactor regex list|add <pattern>|remove <n|pattern> [--profile P]");
        return 2;
    }
    const std::wstring action = WideLower(ctx.opts.positional[1]);

    if (action == L"list") {
        json status;
        if (!ctx.Gate(status)) return 1;
        json profile;
        if (!ctx.SelectProfile(profile)) return 1;
        PrintEntries(ctx, profile["regex_patterns"], "pattern", false);
        return 0;
    }
    if (action == L"add") {
        if (ctx.opts.positional.size() < 3) { ctx.Error(L"regex add requires a pattern"); return 2; }
        std::wstring pattern = ctx.opts.positional[2];
        if (Trim(pattern).empty()) { ctx.Error(L"regex pattern must not be empty"); return 2; }
        // Mirror the GUI's ValidateRegex: reject an unparseable pattern before
        // it reaches the engine/pipeline. The "{,N}" shorthand is normalized
        // (see Utils::NormalizeRegexBraces) so it is accepted and stored in
        // the form the runtime engine can compile.
        pattern = Utils::NormalizeRegexBraces(pattern);
        try {
            std::wregex re(pattern, std::regex_constants::ECMAScript);
        } catch (const std::regex_error& e) {
            ctx.Error(L"invalid regex: " + Utils::Utf8ToWide(e.what()));
            return 2;
        }
        return MutateList(ctx, "regex_patterns", [&](json& entries) {
            entries.push_back({{"pattern", Utils::WideToUtf8(pattern)}, {"enabled", true}});
            ctx.Print(L"ok");
            return true;
        });
    }
    if (action == L"remove") {
        if (ctx.opts.positional.size() < 3) { ctx.Error(L"regex remove requires an index or pattern"); return 2; }
        const std::wstring sel = ctx.opts.positional[2];
        return MutateList(ctx, "regex_patterns", [&](json& entries) {
            return RemoveEntry(ctx, entries, sel, "pattern");
        });
    }
    ctx.Error(L"unknown regex action: " + action + L" (list|add|remove)");
    return 2;
}

int CmdKeywords(const Ctx& ctx) {
    if (ctx.opts.positional.size() < 2) {
        ctx.Error(L"usage: agentredactor keywords list|add <text>|remove <n|text> [--ignore-case] [--profile P]");
        return 2;
    }
    const std::wstring action = WideLower(ctx.opts.positional[1]);

    if (action == L"list") {
        json status;
        if (!ctx.Gate(status)) return 1;
        json profile;
        if (!ctx.SelectProfile(profile)) return 1;
        PrintEntries(ctx, profile["keywords"], "text", true);
        return 0;
    }
    if (action == L"add") {
        if (ctx.opts.positional.size() < 3) { ctx.Error(L"keywords add requires the keyword text"); return 2; }
        const std::wstring text = ctx.opts.positional[2];
        const bool caseSensitive = !ctx.opts.ignoreCase;
        return MutateList(ctx, "keywords", [&](json& entries) {
            entries.push_back({{"text", Utils::WideToUtf8(text)},
                               {"case_sensitive", caseSensitive},
                               {"enabled", true}});
            ctx.Print(L"ok");
            return true;
        });
    }
    if (action == L"remove") {
        if (ctx.opts.positional.size() < 3) { ctx.Error(L"keywords remove requires an index or the keyword text"); return 2; }
        const std::wstring sel = ctx.opts.positional[2];
        return MutateList(ctx, "keywords", [&](json& entries) {
            return RemoveEntry(ctx, entries, sel, "text");
        });
    }
    ctx.Error(L"unknown keywords action: " + action + L" (list|add|remove)");
    return 2;
}

// ---------------------------------------------------------------------------
// usage
// ---------------------------------------------------------------------------

void PrintUsage(const Ctx& ctx) {
    ctx.Print(L"Agent Redactor CLI");
    ctx.Print(L"usage: agentredactor <command> [options]");
    ctx.Print(L"");
    ctx.Print(L"overview:");
    ctx.Print(L"  status                               engine + profile overview");
    ctx.Print(L"  languages                            list supported language codes");
    ctx.Print(L"");
    ctx.Print(L"settings:");
    ctx.Print(L"  get <key> [--profile P]              read a setting");
    ctx.Print(L"  set <key> <value> [--profile P]      change a setting");
    ctx.Print(L"  profiles list                        list profiles");
    ctx.Print(L"  profiles add <alias> [--port N] [--upstream-url U] [--api-key K]");
    ctx.Print(L"  profiles delete <id>");
    ctx.Print(L"");
    PrintValidKeys(ctx);
    ctx.Print(L"");
    ctx.Print(L"redaction lists (profile-scoped):");
    ctx.Print(L"  regex list | add <pattern> | remove <n|pattern>");
    ctx.Print(L"  keywords list | add <text> | remove <n|text> [--ignore-case]");
    ctx.Print(L"  pii-types list [--profile P]");
    ctx.Print(L"  pii-types enable | disable <type> [--profile P]");
    ctx.Print(L"              PII types (single type only): account_number,");
    ctx.Print(L"              private_address, private_date, private_email, private_person,");
    ctx.Print(L"              private_phone, private_url, secret");
    ctx.Print(L"");
    ctx.Print(L"security (Windows Hello only, no typed password):");
    ctx.Print(L"  password enable         enable Windows Hello protection");
    ctx.Print(L"  password disable        disable Windows Hello protection");
    ctx.Print(L"  With protection enabled every read/write command demands a");
    ctx.Print(L"  fresh Windows Hello consent prompt (status/help stay open).");
    ctx.Print(L"");
    ctx.Print(L"options:");
    ctx.Print(L"  --profile P     profile selector: list number, id, or alias");
    ctx.Print(L"  --port N        profiles add: explicit proxy port");
    ctx.Print(L"  --upstream-url U profiles add: upstream endpoint");
    ctx.Print(L"  --api-key K     profiles add: API key");
    ctx.Print(L"  --ignore-case   keywords add: case-insensitive matching");
}

} // namespace

int AgentRedactor::RunCli(const std::vector<std::wstring>& args,
                          const CliTransport& transport,
                          const CliConsole& console) {
    if (args.empty()) {
        Ctx ctx{transport, console, {}};
        PrintUsage(ctx);
        return 2;
    }

    Ctx ctx{transport, console, {}};
    std::wstring parseError;
    if (!ParseOptions(args, ctx.opts, parseError)) {
        ctx.Error(parseError);
        return 2;
    }
    if (ctx.opts.positional.empty()) {
        PrintUsage(ctx);
        return 2;
    }

    const std::wstring cmd = WideLower(ctx.opts.positional[0]);
    if (cmd == L"help" || cmd == L"--help" || cmd == L"-h" || cmd == L"/?") {
        PrintUsage(ctx);
        return 0;
    }
    if (cmd == L"status") return CmdStatus(ctx);
    if (cmd == L"languages") return CmdLanguages(ctx);
    if (cmd == L"password") return CmdPassword(ctx);
    if (cmd == L"get") return CmdGet(ctx);
    if (cmd == L"set") return CmdSet(ctx);
    if (cmd == L"profiles") return CmdProfiles(ctx);
    if (cmd == L"regex") return CmdRegex(ctx);
    if (cmd == L"keywords") return CmdKeywords(ctx);
    if (cmd == L"pii-types") return CmdPiiTypes(ctx);
    // `engine run` / `engine stop` were removed from the CLI surface entirely;
    // engine lifecycle belongs to the GUI (spawn on startup, stop/lock on quit).

    ctx.Error(L"unknown command: " + cmd);
    PrintUsage(ctx);
    return 2;
}
