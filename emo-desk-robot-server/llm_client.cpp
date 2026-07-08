#include "llm_client.hpp"
#include "cJSON/include/cJSON.h"
#include <iostream>
#include <cstring>
#include <sstream>
#include <vector>

#define SPDLOG_HEADER_ONLY
#include <spdlog/spdlog.h>

LlmClient::LlmClient()
    : api_url_("https://api.deepseek.com/v1/chat/completions")
    , model_("deepseek-chat")
{
}

void LlmClient::set_api_key(const std::string& key) { api_key_ = key; }
void LlmClient::set_api_url(const std::string& url) { api_url_ = url; }
void LlmClient::set_model(const std::string& model) { model_ = model; }
void LlmClient::set_system_prompt(const std::string& prompt) { system_prompt_ = prompt; }

// Cross-platform UTF-8 to \uXXXX JSON escaping
static std::string json_escape(const std::string& s)
{
    std::string out;
    size_t i = 0;
    while (i < s.length()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04X", c);
                        out += buf;
                    } else {
                        out += s[i];
                    }
                    break;
            }
            i++;
            continue;
        }

        unsigned int codepoint = 0;
        size_t extra = 0;
        if ((c & 0xE0) == 0xC0)      { codepoint = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { codepoint = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { codepoint = c & 0x07; extra = 3; }
        else { out += '?'; i++; continue; }

        if (i + extra >= s.length()) { out += '?'; i++; continue; }

        bool valid = true;
        for (size_t j = 1; j <= extra; j++) {
            unsigned char next = static_cast<unsigned char>(s[i + j]);
            if ((next & 0xC0) != 0x80) { valid = false; break; }
            codepoint = (codepoint << 6) | (next & 0x3F);
        }
        if (!valid) { out += '?'; i++; continue; }

        if (codepoint <= 0xFFFF) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04X", codepoint);
            out += buf;
        } else {
            unsigned int hi = 0xD800 + ((codepoint - 0x10000) >> 10);
            unsigned int lo = 0xDC00 + ((codepoint - 0x10000) & 0x3FF);
            char b1[8], b2[8];
            snprintf(b1, sizeof(b1), "\\u%04X", hi);
            snprintf(b2, sizeof(b2), "\\u%04X", lo);
            out += b1; out += b2;
        }
        i += 1 + extra;
    }
    return out;
}

void LlmClient::clear_history()
{
    history_.clear();
}

std::string LlmClient::build_request_body(const std::string& user_message) const
{
    std::ostringstream body;
    body << "{\"model\":\"" << json_escape(model_)
         << "\",\"temperature\":0.7,\"messages\":[";

    if (!system_prompt_.empty()) {
        body << "{\"role\":\"system\",\"content\":\""
             << json_escape(system_prompt_) << "\"},";
    }

    // 插入对话历史（最多20条，即最近10轮）
    size_t start = (history_.size() > 20) ? history_.size() - 20 : 0;
    for (size_t i = start; i < history_.size(); i++) {
        body << "{\"role\":\"" << history_[i].first << "\",\"content\":\""
             << json_escape(history_[i].second) << "\"},";
    }

    body << "{\"role\":\"user\",\"content\":\""
         << json_escape(user_message) << "\"}]}";

    return body.str();
}

static std::string safe_get_string(cJSON* item)
{
    if (cJSON_IsString(item) && item->valuestring)
        return item->valuestring;
    return "";
}

std::string LlmClient::parse_response(const std::string& response_body) const
{
    cJSON* root = cJSON_Parse(response_body.c_str());
    if (!root) {
        return "[Error] Failed to parse API response (invalid JSON)";
    }

    cJSON* error = cJSON_GetObjectItem(root, "error");
    if (error && !cJSON_IsNull(error)) {
        cJSON* err_msg = cJSON_GetObjectItem(error, "message");
        std::string result = "[API Error] " + safe_get_string(err_msg);
        cJSON_Delete(root);
        return result;
    }

    cJSON* choices = cJSON_GetObjectItem(root, "choices");
    if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON* choice = cJSON_GetArrayItem(choices, 0);
        cJSON* message = cJSON_GetObjectItem(choice, "message");
        cJSON* content = cJSON_GetObjectItem(message, "content");
        std::string result = safe_get_string(content);
        cJSON_Delete(root);
        if (!result.empty()) return result;
        return "[Error] Empty response content";
    }

    cJSON_Delete(root);
    return "[Error] Unexpected API response format";
}

// =========================================================================
// Windows: use WinHTTP (native, no extra dependencies)
// =========================================================================
#ifdef _WIN32

#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

std::string LlmClient::chat(const std::string& user_message)
{
    if (api_key_.empty()) {
        return "[Error] API key not configured. Please set it in config.json";
    }

    history_.emplace_back("user", user_message);

    std::string request_body = build_request_body(user_message);

    HINTERNET hSession = nullptr;
    HINTERNET hConnect = nullptr;
    HINTERNET hRequest = nullptr;

    auto cleanup = [&]() {
        if (hRequest) WinHttpCloseHandle(hRequest);
        if (hConnect) WinHttpCloseHandle(hConnect);
        if (hSession) WinHttpCloseHandle(hSession);
    };

    hSession = WinHttpOpen(L"emo-desk-robot-server/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);

    if (!hSession) {
        return "[Error] Failed to initialize HTTP client";
    }

    URL_COMPONENTS urlComp = { sizeof(URL_COMPONENTS) };
    wchar_t hostName[256] = {0};
    wchar_t urlPath[2048] = {0};
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = 2048;
    urlComp.dwSchemeLength = 1;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, api_url_.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wurl(wlen);
    MultiByteToWideChar(CP_UTF8, 0, api_url_.c_str(), -1, wurl.data(), wlen);

    if (!WinHttpCrackUrl(wurl.data(), (DWORD)wurl.size() - 1, 0, &urlComp)) {
        cleanup();
        return "[Error] Invalid API URL";
    }

    hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
    if (!hConnect) {
        cleanup();
        return "[Error] Failed to connect to API server";
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    hRequest = WinHttpOpenRequest(hConnect, L"POST", urlPath,
        nullptr, nullptr, nullptr, flags);
    if (!hRequest) {
        cleanup();
        return "[Error] Failed to create HTTP request";
    }

    int key_len = MultiByteToWideChar(CP_UTF8, 0, api_key_.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wkey(key_len);
    MultiByteToWideChar(CP_UTF8, 0, api_key_.c_str(), -1, wkey.data(), key_len);
    std::wstring auth_header = L"Authorization: Bearer " + std::wstring(wkey.data());

    WinHttpAddRequestHeaders(hRequest, L"Content-Type: application/json",
        (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);
    WinHttpAddRequestHeaders(hRequest, auth_header.c_str(),
        (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);
    WinHttpAddRequestHeaders(hRequest, L"Accept: application/json",
        (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(hRequest, nullptr, 0,
            (LPVOID)request_body.c_str(), (DWORD)request_body.length(),
            (DWORD)request_body.length(), 0)) {
        cleanup();
        return "[Error] Failed to send request";
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        cleanup();
        return "[Error] Failed to receive response";
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        nullptr, &statusCode, &statusSize, nullptr);

    std::string response_body;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buffer(bytesAvailable + 1, 0);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead)) {
            response_body.append(buffer.data(), bytesRead);
        }
    }

    cleanup();

    if (statusCode != 200) {
        std::string error = "[API Error] HTTP " + std::to_string(statusCode);
        spdlog::warn("LLM API returned HTTP {} (body length: {})", statusCode, response_body.length());
        if (!response_body.empty()) {
            cJSON* root = cJSON_Parse(response_body.c_str());
            if (root) {
                cJSON* err = cJSON_GetObjectItemCaseSensitive(root, "error");
                if (cJSON_IsObject(err)) {
                    cJSON* msg = cJSON_GetObjectItemCaseSensitive(err, "message");
                    if (cJSON_IsString(msg) && msg->valuestring)
                        error += ": " + std::string(msg->valuestring);
                    cJSON* type = cJSON_GetObjectItemCaseSensitive(err, "type");
                    if (cJSON_IsString(type) && type->valuestring)
                        error += " [" + std::string(type->valuestring) + "]";
                }
                cJSON_Delete(root);
            }
        }
        return error;
    }

    std::string result = parse_response(response_body);
    if (result.find("[Error]") == std::string::npos) {
        history_.emplace_back("assistant", result);
    }
    return result;
}

// =========================================================================
// Linux / macOS: use httplib (requires OpenSSL)
// =========================================================================
#else

#include "httplib.h"

std::string LlmClient::chat(const std::string& user_message)
{
    if (api_key_.empty()) {
        return "[Error] API key not configured. Please set it in config.json";
    }

    history_.emplace_back("user", user_message);

    std::string request_body = build_request_body(user_message);
    spdlog::debug("Sending {} bytes to LLM", request_body.length());

    std::string scheme, host, path;
    int port = 443;

    size_t scheme_end = api_url_.find("://");
    if (scheme_end == std::string::npos)
        return "[Error] Invalid API URL: missing scheme";
    scheme = api_url_.substr(0, scheme_end);

    size_t path_start = api_url_.find('/', scheme_end + 3);
    if (path_start == std::string::npos) {
        host = api_url_.substr(scheme_end + 3);
        path = "/";
    } else {
        host = api_url_.substr(scheme_end + 3, path_start - scheme_end - 3);
        path = api_url_.substr(path_start);
    }

    httplib::Headers headers = {
        {"Content-Type", "application/json"},
        {"Authorization", "Bearer " + api_key_},
        {"Accept", "application/json"}
    };

    auto cli = std::make_shared<httplib::SSLClient>(host, port);
    cli->enable_server_certificate_verification(true);
    cli->set_connection_timeout(std::chrono::seconds(10));
    cli->set_read_timeout(std::chrono::seconds(30));
    cli->set_write_timeout(std::chrono::seconds(10));

    auto res = cli->Post(path.c_str(), headers, request_body, "application/json");

    if (!res) {
        std::string err_str;
        auto err = res.error();
        switch (err) {
            case httplib::Error::Connection:          err_str = "Connection failed"; break;
            case httplib::Error::Bind:                err_str = "Bind failed"; break;
            case httplib::Error::Read:                err_str = "Read error"; break;
            case httplib::Error::Write:               err_str = "Write error"; break;
            case httplib::Error::ExceedRedirectCount: err_str = "Too many redirects"; break;
            case httplib::Error::Canceled:            err_str = "Request canceled"; break;
            case httplib::Error::SSLConnection:       err_str = "SSL connection failed"; break;
            case httplib::Error::SSLLoadingCerts:     err_str = "SSL cert loading failed"; break;
            case httplib::Error::SSLServerVerification: err_str = "SSL server verification failed"; break;
            case httplib::Error::ConnectionTimeout:   err_str = "Connection timeout"; break;
            default: err_str = "Unknown error (" + std::to_string(static_cast<int>(err)) + ")"; break;
        }
        spdlog::error("LLM request failed: {}", err_str);
        return "[Error] " + err_str;
    }

    if (res->status != 200) {
        std::string error = "[API Error] HTTP " + std::to_string(res->status);
        spdlog::warn("LLM API returned HTTP {}", res->status);
        if (!res->body.empty()) {
            cJSON* root = cJSON_Parse(res->body.c_str());
            if (root) {
                cJSON* err = cJSON_GetObjectItemCaseSensitive(root, "error");
                if (cJSON_IsObject(err)) {
                    cJSON* msg = cJSON_GetObjectItemCaseSensitive(err, "message");
                    if (cJSON_IsString(msg) && msg->valuestring)
                        error += ": " + std::string(msg->valuestring);
                }
                cJSON_Delete(root);
            }
        }
        return error;
    }

    std::string result = parse_response(res->body);
    if (result.find("[Error]") == std::string::npos) {
        history_.emplace_back("assistant", result);
    }
    return result;
}

#endif
