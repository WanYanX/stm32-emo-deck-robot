#include "mimo_voice_client.hpp"
#include "cJSON/include/cJSON.h"
#include <sstream>
#include <stdexcept>

#define SPDLOG_HEADER_ONLY
#include <spdlog/spdlog.h>

static std::string json_escape_voice(const std::string& s)
{
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
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
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

static std::string base64_encode(const unsigned char* data, size_t len)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned int v = data[i] << 16;
        if (i + 1 < len) v |= data[i + 1] << 8;
        if (i + 2 < len) v |= data[i + 2];
        out.push_back(table[(v >> 18) & 0x3F]);
        out.push_back(table[(v >> 12) & 0x3F]);
        out.push_back(i + 1 < len ? table[(v >> 6) & 0x3F] : '=');
        out.push_back(i + 2 < len ? table[v & 0x3F] : '=');
    }
    return out;
}

static std::vector<unsigned char> base64_decode(const std::string& input)
{
    static const int bad = -1;
    int table[256];
    std::fill(std::begin(table), std::end(table), bad);
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < static_cast<int>(chars.size()); ++i)
        table[static_cast<unsigned char>(chars[i])] = i;

    std::vector<unsigned char> out;
    int val = 0;
    int valb = -8;
    for (unsigned char c : input) {
        if (c == '=') break;
        if (table[c] == bad) continue;
        val = (val << 6) + table[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<unsigned char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

static std::string safe_json_string(cJSON* item)
{
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

void MimoVoiceClient::set_api_key(const std::string& key) { api_key_ = key; }
void MimoVoiceClient::set_asr_url(const std::string& url) { asr_url_ = url; }
void MimoVoiceClient::set_asr_model(const std::string& model) { asr_model_ = model; }
void MimoVoiceClient::set_tts_url(const std::string& url) { tts_url_ = url; }
void MimoVoiceClient::set_tts_model(const std::string& model) { tts_model_ = model; }
void MimoVoiceClient::set_tts_voice(const std::string& voice) { tts_voice_ = voice; }

std::string MimoVoiceClient::recognize_wav(const std::vector<unsigned char>& wav_bytes, const std::string& language) const
{
    if (api_key_.empty())
        return "[Error] MIMO API key not configured";
    if (wav_bytes.empty())
        return "";

    std::string audio_base64 = base64_encode(wav_bytes.data(), wav_bytes.size());
    std::ostringstream body;
    body << "{\"model\":\"" << json_escape_voice(asr_model_) << "\",\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"input_audio\",\"input_audio\":{\"data\":\"data:audio/wav;base64,"
         << audio_base64
         << "\"}}]}],\"extra_body\":{\"asr_options\":{\"language\":\"" << json_escape_voice(language) << "\"}}}";

    std::string response = post_json(asr_url_, body.str());
    cJSON* root = cJSON_Parse(response.c_str());
    if (!root)
        return "[Error] Failed to parse ASR response";

    std::string text;
    cJSON* choices = cJSON_GetObjectItem(root, "choices");
    if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON* choice = cJSON_GetArrayItem(choices, 0);
        cJSON* message = cJSON_GetObjectItem(choice, "message");
        cJSON* content = cJSON_GetObjectItem(message, "content");
        text = safe_json_string(content);
    }
    cJSON_Delete(root);
    return text.empty() ? "[Error] Empty ASR text" : text;
}

std::vector<unsigned char> MimoVoiceClient::synthesize_wav(const std::string& text) const
{
    if (api_key_.empty() || text.empty())
        return {};

    std::ostringstream body;
    body << "{\"model\":\"" << json_escape_voice(tts_model_) << "\",\"messages\":[{\"role\":\"assistant\",\"content\":\""
         << json_escape_voice(text)
         << "\"}],\"audio\":{\"format\":\"wav\",\"voice\":\"" << json_escape_voice(tts_voice_) << "\"}}";

    std::string response = post_json(tts_url_, body.str());
    cJSON* root = cJSON_Parse(response.c_str());
    if (!root)
        return {};

    std::string audio_base64;
    cJSON* choices = cJSON_GetObjectItem(root, "choices");
    if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON* choice = cJSON_GetArrayItem(choices, 0);
        cJSON* message = cJSON_GetObjectItem(choice, "message");
        cJSON* audio = cJSON_GetObjectItem(message, "audio");
        cJSON* data = cJSON_GetObjectItem(audio, "data");
        audio_base64 = safe_json_string(data);
    }
    cJSON_Delete(root);
    return audio_base64.empty() ? std::vector<unsigned char>{} : base64_decode(audio_base64);
}

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>

std::string MimoVoiceClient::post_json(const std::string& url, const std::string& body) const
{
    HINTERNET hSession = nullptr, hConnect = nullptr, hRequest = nullptr;
    auto cleanup = [&]() {
        if (hRequest) WinHttpCloseHandle(hRequest);
        if (hConnect) WinHttpCloseHandle(hConnect);
        if (hSession) WinHttpCloseHandle(hSession);
    };

    hSession = WinHttpOpen(L"emo-desk-robot-mimo/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!hSession) return "{}";

    URL_COMPONENTS urlComp = { sizeof(URL_COMPONENTS) };
    wchar_t hostName[256] = {0};
    wchar_t urlPath[2048] = {0};
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = 2048;
    urlComp.dwSchemeLength = 1;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wurl(static_cast<size_t>(wlen));
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wurl.data(), wlen);

    if (!WinHttpCrackUrl(wurl.data(), static_cast<DWORD>(wurl.size() - 1), 0, &urlComp)) {
        cleanup();
        return "{}";
    }

    hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
    if (!hConnect) { cleanup(); return "{}"; }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    hRequest = WinHttpOpenRequest(hConnect, L"POST", urlPath, nullptr, nullptr, nullptr, flags);
    if (!hRequest) { cleanup(); return "{}"; }

    int key_len = MultiByteToWideChar(CP_UTF8, 0, api_key_.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wkey(static_cast<size_t>(key_len));
    MultiByteToWideChar(CP_UTF8, 0, api_key_.c_str(), -1, wkey.data(), key_len);
    std::wstring auth = L"Authorization: Bearer " + std::wstring(wkey.data());

    WinHttpAddRequestHeaders(hRequest, L"Content-Type: application/json", (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);
    WinHttpAddRequestHeaders(hRequest, L"Accept: application/json", (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);
    WinHttpAddRequestHeaders(hRequest, auth.c_str(), (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(hRequest, nullptr, 0, (LPVOID)body.c_str(), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) ||
        !WinHttpReceiveResponse(hRequest, nullptr)) {
        cleanup();
        return "{}";
    }

    std::string response;
    DWORD available = 0;
    while (WinHttpQueryDataAvailable(hRequest, &available) && available > 0) {
        std::vector<char> buffer(static_cast<size_t>(available) + 1, 0);
        DWORD read = 0;
        if (WinHttpReadData(hRequest, buffer.data(), available, &read))
            response.append(buffer.data(), read);
    }

    cleanup();
    return response;
}
#else
#include "httplib.h"

std::string MimoVoiceClient::post_json(const std::string& url, const std::string& body) const
{
    size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return "{}";
    size_t path_start = url.find('/', scheme_end + 3);
    std::string host = path_start == std::string::npos ? url.substr(scheme_end + 3) : url.substr(scheme_end + 3, path_start - scheme_end - 3);
    std::string path = path_start == std::string::npos ? "/" : url.substr(path_start);
    httplib::Headers headers = {{"Content-Type", "application/json"}, {"Accept", "application/json"}, {"Authorization", "Bearer " + api_key_}};
    httplib::SSLClient cli(host, 443);
    auto res = cli.Post(path.c_str(), headers, body, "application/json");
    return res ? res->body : "{}";
}
#endif
