// OS-agnostic CLI command implementation. See cli.h for the design notes.
#include "cli.h"
#include "utils.h"

#include <cwctype>
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

std::vector<std::wstring> SplitCsv(const std::wstring& s) {
    std::vector<std::wstring> out;
    std::wstringstream ss(s);
    std::wstring item;
    while (std::getline(ss, item, L',')) {
        item = Trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

std::wstring JoinCsv(const json& arr) {
    std::wstring out;
    if (!arr.is_array()) return out;
    for (const auto& v : arr) {
        if (!out.empty()) out += L", ";
        out += Utils::Utf8ToWide(v.get<std::string>());
    }
    return out;
}

// ---------------------------------------------------------------------------
// Invocation context
// ---------------------------------------------------------------------------

struct CliOptions {
    std::vector<std::wstring> positional;
    std::optional<std::wstring> profile;
    std::optional<std::wstring> password;
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
        r = flagged(L"--password", opts.password);
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
        Error(L"engine is not running (start it with 'agentredactor engine run')");
        return false;
    }

    std::optional<std::wstring> GetPassword() const {
        if (opts.password) return opts.password;
        if (c.promptPassword) {
            auto pw = c.promptPassword();
            if (pw) return pw;
        }
        Error(L"password required and no interactive console available; pass --password <pw>");
        return std::nullopt;
    }

    bool UnlockWith(const std::wstring& password) const {
        json out;
        if (!t.post(L"/unlock", json{{"password", Utils::WideToUtf8(password)}}, &out)) {
            Error(L"unlock request failed");
            return false;
        }
        if (out.value("ok", false)) return true;
        Error(L"wrong password");
        return false;
    }

    // Master-password gate for every settings/profile command. Open when no
    // password is set; otherwise unlocks the session first.
    bool EnsureUnlocked(const json& status) const {
        if (!status.value("masterPasswordEnabled", false)) return true;
        if (status.value("unlocked", false)) return true;
        auto pw = GetPassword();
        if (!pw) return false;
        return UnlockWith(*pw);
    }

    // Combined precondition used by all gated commands.
    bool Gate(json& status) const {
        if (!EngineStatus(status)) return false;
        return EnsureUnlocked(status);
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
// status / engine stop / unlock / password
// ---------------------------------------------------------------------------

int CmdStatus(const Ctx& ctx) {
    json status;
    if (!ctx.EngineStatus(status)) return 1;

    ctx.Print(L"engine:          " + Utils::Utf8ToWide(status.value("engineVersion", std::string("?"))));
    ctx.Print(L"master password: " + BoolStr(status.value("masterPasswordEnabled", false)));
    ctx.Print(L"session:         " + std::wstring(status.value("unlocked", false) ? L"unlocked" : L"locked"));
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
                + (sp.value("enabled", false) ? L"  enabled" : L"  disabled")
                + (sp.value("proxyRunning", false) ? L"  proxy running" : L"  proxy stopped")
                + L"  requests=" + std::to_wstring(requests)
                + L"  redactions=" + std::to_wstring(redactions);
            ctx.Print(line);
        }
    }
    return 0;
}

int CmdEngineStop(const Ctx& ctx) {
    json out;
    if (!ctx.t.post(L"/engine/stop", json::object(), &out)) {
        ctx.Error(L"engine is not running (or stop request failed)");
        return 1;
    }
    ctx.Print(L"engine stopping");
    return 0;
}

int CmdUnlock(const Ctx& ctx) {
    json status;
    if (!ctx.EngineStatus(status)) return 1;
    if (!status.value("masterPasswordEnabled", false)) {
        ctx.Print(L"master password is not enabled");
        return 0;
    }
    if (status.value("unlocked", false)) {
        ctx.Print(L"already unlocked");
        return 0;
    }
    auto pw = ctx.GetPassword();
    if (!pw) return 1;
    if (!ctx.UnlockWith(*pw)) return 1;
    ctx.Print(L"unlocked");
    return 0;
}

int CmdPassword(const Ctx& ctx) {
    if (ctx.opts.positional.size() < 2) {
        ctx.Error(L"usage: agentredactor password enable [pw] | change [old new] | disable");
        return 2;
    }
    const std::wstring action = WideLower(ctx.opts.positional[1]);
    json status;
    if (!ctx.EngineStatus(status)) return 1;

    if (action == L"enable") {
        if (status.value("masterPasswordEnabled", false)) {
            ctx.Error(L"master password is already enabled (use 'password change')");
            return 1;
        }
        std::wstring pw;
        if (ctx.opts.positional.size() >= 3) pw = ctx.opts.positional[2];
        else {
            auto prompted = ctx.GetPassword();
            if (!prompted) return 1;
            pw = *prompted;
        }
        if (pw.empty()) { ctx.Error(L"password must not be empty"); return 2; }
        if (!ctx.t.put(L"/settings/enableMasterPassword", json{{"value", Utils::WideToUtf8(pw)}}, nullptr)) {
            ctx.Error(L"failed to enable the master password");
            return 1;
        }
        ctx.Print(L"master password enabled");
        return 0;
    }

    if (action == L"change") {
        if (!ctx.EnsureUnlocked(status)) return 1;
        std::wstring oldPw, newPw;
        if (ctx.opts.positional.size() >= 4) {
            oldPw = ctx.opts.positional[2];
            newPw = ctx.opts.positional[3];
        } else {
            auto p1 = ctx.GetPassword();
            if (!p1) return 1;
            auto p2 = ctx.GetPassword();
            if (!p2) return 1;
            oldPw = *p1; newPw = *p2;
        }
        if (newPw.empty()) { ctx.Error(L"new password must not be empty"); return 2; }
        json body{{"oldValue", Utils::WideToUtf8(oldPw)}, {"value", Utils::WideToUtf8(newPw)}};
        if (!ctx.t.put(L"/settings/changeMasterPassword", body, nullptr)) {
            ctx.Error(L"failed to change the master password (wrong current password?)");
            return 1;
        }
        ctx.Print(L"master password changed");
        return 0;
    }

    if (action == L"disable") {
        if (!status.value("masterPasswordEnabled", false)) {
            ctx.Print(L"master password is not enabled");
            return 0;
        }
        if (!ctx.EnsureUnlocked(status)) return 1;
        if (!ctx.t.put(L"/settings/disableMasterPassword", json{{"value", true}}, nullptr)) {
            ctx.Error(L"failed to disable the master password");
            return 1;
        }
        ctx.Print(L"master password disabled");
        return 0;
    }

    ctx.Error(L"unknown password action: " + action + L" (enable|change|disable)");
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
    {L"onnx-provider", "onnxProvider", 's'},
    {L"logging", "loggingEnabled", 'b'},
    {L"show-sensitive", "showSensitive", 'b'},
    {L"app-language", "appLanguage", 's'},
    {L"master-password-enabled", "masterPasswordEnabled", 'r'},
    {L"unlocked", "unlocked", 'r'},
};

struct ProfileKey {
    const wchar_t* cliName;
    const char* jsonName;
    char type; // 'b' bool, 's' string, 'i' int, 'f' float, 'l' csv list
};

constexpr ProfileKey kProfileKeys[] = {
    {L"alias", "alias", 's'},
    {L"upstream-url", "upstream_url", 's'},
    {L"port", "port", 'i'},
    {L"enabled", "enabled", 'b'},
    {L"confidence-threshold", "pii_confidence_threshold", 'f'},
    {L"pii-types", "enabled_pii_types", 'l'},
    {L"use-openai-model", "use_openai_model", 'b'},
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
    ctx.Print(L"global keys:  start-on-boot, onnx-provider, logging, show-sensitive, app-language,");
    ctx.Print(L"              master-password-enabled, unlocked (read-only)");
    ctx.Print(L"profile keys: alias, upstream-url, api-key, port, enabled,");
    ctx.Print(L"              confidence-threshold, pii-types, use-openai-model");
}

int CmdGet(const Ctx& ctx) {
    if (ctx.opts.positional.size() < 2) {
        ctx.Error(L"usage: agentredactor get <key> [--profile <n|id|alias>]");
        return 2;
    }
    const std::wstring key = WideLower(ctx.opts.positional[1]);

    // Every read is gated when a master password is set (the engine lock is
    // UX-level, mirroring the GUI); only status/engine-stop/help stay open.
    json status;
    if (!ctx.Gate(status)) return 1;

    if (key == L"api-key") {
        json profile;
        if (!ctx.SelectProfile(profile)) return 1;
        json out;
        if (!ctx.t.get(L"/profiles/" + JStr(profile, "id") + L"/apikey", out)) {
            ctx.Error(L"failed to read the API key (session locked?)");
            return 1;
        }
        ctx.Print(Utils::Utf8ToWide(out.value("apiKey", std::string(""))));
        return 0;
    }

    if (const GlobalKey* gk = FindGlobalKey(key)) {
        json settings;
        if (!ctx.t.get(L"/settings", settings)) {
            ctx.Error(L"engine is not running (start it with 'agentredactor engine run')");
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
        case 'l': ctx.Print(JoinCsv(v)); return 0;
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

    if (key == L"master-password-enabled" || key == L"unlocked") {
        ctx.Error(key + L" is read-only (use 'agentredactor password ...' / 'unlock')");
        return 2;
    }

    json status;
    if (!ctx.Gate(status)) return 1;

    if (const GlobalKey* gk = FindGlobalKey(key)) {
        json body;
        if (gk->type == 'b') {
            bool b;
            if (!ParseBool(value, b)) { ctx.Error(L"expected a boolean (true/false), got: " + value); return 2; }
            body["value"] = b;
        } else {
            body["value"] = Utils::WideToUtf8(value);
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

    switch (pk->type) {
    case 'b': {
        bool b;
        if (!ParseBool(value, b)) { ctx.Error(L"expected a boolean (true/false), got: " + value); return 2; }
        profile[pk->jsonName] = b;
        break;
    }
    case 'i': {
        if (!IsInteger(value)) { ctx.Error(L"expected an integer, got: " + value); return 2; }
        profile[pk->jsonName] = std::stoi(value);
        break;
    }
    case 'f': {
        float f;
        try { f = std::stof(value); } catch (...) { ctx.Error(L"expected a number, got: " + value); return 2; }
        if (f < 0.0f || f > 1.0f) { ctx.Error(L"confidence-threshold must be between 0 and 1"); return 2; }
        profile[pk->jsonName] = f;
        break;
    }
    case 'l': {
        json arr = json::array();
        for (const auto& item : SplitCsv(value)) arr.push_back(Utils::WideToUtf8(item));
        profile[pk->jsonName] = arr;
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

int CmdProfiles(const Ctx& ctx) {
    if (ctx.opts.positional.size() < 2 || WideLower(ctx.opts.positional[1]) != L"list") {
        ctx.Error(L"usage: agentredactor profiles list");
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
        ctx.Print(L"no profiles configured");
        return 0;
    }

    std::vector<std::vector<std::wstring>> rows;
    rows.push_back({L"#", L"alias", L"id", L"port", L"enabled", L"running", L"requests", L"redactions"});
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
            BoolStr(p.value("enabled", false)),
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
        const std::wstring pattern = ctx.opts.positional[2];
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
    ctx.Print(L"engine:");
    ctx.Print(L"  (no args) | engine run [--console]   run the engine (hidden / foreground)");
    ctx.Print(L"  engine stop                          stop the running engine");
    ctx.Print(L"  status                               engine + profile overview");
    ctx.Print(L"");
    ctx.Print(L"settings:");
    ctx.Print(L"  get <key> [--profile P]              read a setting");
    ctx.Print(L"  set <key> <value> [--profile P]      change a setting");
    ctx.Print(L"  profiles list                        list profiles");
    ctx.Print(L"");
    PrintValidKeys(ctx);
    ctx.Print(L"");
    ctx.Print(L"redaction lists (profile-scoped):");
    ctx.Print(L"  regex list | add <pattern> | remove <n|pattern>");
    ctx.Print(L"  keywords list | add <text> | remove <n|text> [--ignore-case]");
    ctx.Print(L"");
    ctx.Print(L"security:");
    ctx.Print(L"  unlock [--password PW]");
    ctx.Print(L"  password enable [PW] | change [OLD NEW] | disable");
    ctx.Print(L"");
    ctx.Print(L"options:");
    ctx.Print(L"  --profile P     profile selector: list number, id, or alias");
    ctx.Print(L"  --password PW   master password (skips the interactive prompt)");
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
    if (cmd == L"unlock") return CmdUnlock(ctx);
    if (cmd == L"password") return CmdPassword(ctx);
    if (cmd == L"get") return CmdGet(ctx);
    if (cmd == L"set") return CmdSet(ctx);
    if (cmd == L"profiles") return CmdProfiles(ctx);
    if (cmd == L"regex") return CmdRegex(ctx);
    if (cmd == L"keywords") return CmdKeywords(ctx);
    if (cmd == L"engine") {
        if (ctx.opts.positional.size() >= 2 && WideLower(ctx.opts.positional[1]) == L"stop") {
            return CmdEngineStop(ctx);
        }
        // `engine run` is handled by the OS entry point before reaching here.
        ctx.Error(L"usage: agentredactor engine run [--console] | stop");
        return 2;
    }

    ctx.Error(L"unknown command: " + cmd);
    PrintUsage(ctx);
    return 2;
}
