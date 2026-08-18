#include "control_api_client.h"
#include "utils.h"
#include <curl/curl.h>

using namespace AgentRedactor;

namespace {

size_t WriteToString(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

} // namespace

bool ControlApiClient::Connect(const std::filesystem::path& configDir) {
    port_ = 0;
    token_.clear();

    auto content = Utils::ReadFileAsString(configDir / L"control.json");
    if (!content) return false;
    try {
        json j = json::parse(Utils::WideToUtf8(*content));
        port_ = j.at("port").get<int>();
        token_ = Utils::Utf8ToWide(j.at("token").get<std::string>());
    } catch (...) {
        port_ = 0;
        token_.clear();
        return false;
    }
    return IsConnected();
}

bool ControlApiClient::Get(const std::wstring& path, json& out) const {
    long status = 0;
    std::string body;
    if (!Request(L"GET", path, nullptr, status, body) || status != 200) return false;
    try {
        out = json::parse(body);
    } catch (...) {
        return false;
    }
    return true;
}

bool ControlApiClient::Post(const std::wstring& path, const json& body, json* out) const {
    std::string payload = body.dump();
    long status = 0;
    std::string respBody;
    if (!Request(L"POST", path, &payload, status, respBody) || status != 200) return false;
    if (out) {
        try {
            *out = json::parse(respBody);
        } catch (...) {
            return false;
        }
    }
    return true;
}

bool ControlApiClient::Put(const std::wstring& path, const json& body, json* out) const {
    std::string payload = body.dump();
    long status = 0;
    std::string respBody;
    if (!Request(L"PUT", path, &payload, status, respBody) || status != 200) return false;
    if (out) {
        try {
            *out = json::parse(respBody);
        } catch (...) {
            return false;
        }
    }
    return true;
}

bool ControlApiClient::Delete(const std::wstring& path) const {
    long status = 0;
    std::string body;
    return Request(L"DELETE", path, nullptr, status, body) && status == 200;
}

bool ControlApiClient::UnlockWithPassword(const std::wstring& password) const {
    json out;
    if (!Post(L"/unlock", json{{"password", Utils::WideToUtf8(password)}}, &out)) return false;
    return out.value("ok", false);
}

bool ControlApiClient::Request(const std::wstring& method, const std::wstring& path,
    const std::string* body, long& statusCode, std::string& responseBody) const {
    statusCode = 0;
    responseBody.clear();
    if (!IsConnected()) return false;

    CURL* curl = curl_easy_init();
    if (!curl) return false;

    const std::string url = "http://127.0.0.1:" + std::to_string(port_) + Utils::WideToUtf8(path);
    const std::string auth = "Authorization: Bearer " + Utils::WideToUtf8(token_);
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, auth.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    // The CLI only sends JSON ASCII/UTF-8 bodies; suppress 100-continue so the
    // engine's simple HTTP parser never sees an interim response.
    headers = curl_slist_append(headers, "Expect:");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, Utils::WideToUtf8(method).c_str());
    if (body) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body->data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body->size()));
    }
    // Short timeouts: the engine is localhost; a hang means it is not running.
    // /unlock derives a PBKDF2 key (sub-second) so the default suffices.
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1500L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);

    const CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return res == CURLE_OK;
}
