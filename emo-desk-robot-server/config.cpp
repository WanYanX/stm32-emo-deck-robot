#include "config.hpp"
#include "cJSON/include/cJSON.h"
#include <fstream>
#include <sstream>

#define SPDLOG_HEADER_ONLY
#include <spdlog/spdlog.h>

Config& Config::get_instance()
{
    static Config instance;
    return instance;
}

bool Config::load(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        spdlog::error("Failed to open config file: {}", path);
        return false;
    }

    std::stringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();

    cJSON* root = cJSON_Parse(content.c_str());
    if (!root) {
        spdlog::error("Failed to parse config file");
        return false;
    }

    cJSON* port_item = cJSON_GetObjectItem(root, "port");
    if (cJSON_IsNumber(port_item)) {
        port_ = port_item->valueint;
    }

    cJSON* ip_item = cJSON_GetObjectItem(root, "server_ip");
    if (cJSON_IsString(ip_item) && ip_item->valuestring) {
        server_ip_ = ip_item->valuestring;
    }

    cJSON* llm = cJSON_GetObjectItem(root, "llm");
    if (cJSON_IsObject(llm)) {
        cJSON* key = cJSON_GetObjectItem(llm, "api_key");
        if (cJSON_IsString(key) && key->valuestring)
            api_key_ = key->valuestring;

        cJSON* url = cJSON_GetObjectItem(llm, "api_url");
        if (cJSON_IsString(url) && url->valuestring)
            api_url_ = url->valuestring;

        cJSON* model = cJSON_GetObjectItem(llm, "model");
        if (cJSON_IsString(model) && model->valuestring)
            model_ = model->valuestring;

        cJSON* prompt = cJSON_GetObjectItem(llm, "system_prompt");
        if (cJSON_IsString(prompt) && prompt->valuestring)
            system_prompt_ = prompt->valuestring;
    }

    cJSON* ws = cJSON_GetObjectItem(root, "websocket");
    if (cJSON_IsObject(ws)) {
        cJSON* ws_port = cJSON_GetObjectItem(ws, "port");
        if (cJSON_IsNumber(ws_port))
            ws_port_ = ws_port->valueint;

        cJSON* ws_path = cJSON_GetObjectItem(ws, "path");
        if (cJSON_IsString(ws_path) && ws_path->valuestring)
            ws_path_ = ws_path->valuestring;

        cJSON* ws_token = cJSON_GetObjectItem(ws, "token");
        if (cJSON_IsString(ws_token) && ws_token->valuestring)
            ws_token_ = ws_token->valuestring;
    }

    cJSON* mimo = cJSON_GetObjectItem(root, "mimo");
    if (cJSON_IsObject(mimo)) {
        cJSON* key = cJSON_GetObjectItem(mimo, "api_key");
        if (cJSON_IsString(key) && key->valuestring)
            mimo_api_key_ = key->valuestring;

        cJSON* asr_url = cJSON_GetObjectItem(mimo, "asr_url");
        if (cJSON_IsString(asr_url) && asr_url->valuestring)
            mimo_asr_url_ = asr_url->valuestring;

        cJSON* asr_model = cJSON_GetObjectItem(mimo, "asr_model");
        if (cJSON_IsString(asr_model) && asr_model->valuestring)
            mimo_asr_model_ = asr_model->valuestring;

        cJSON* tts_url = cJSON_GetObjectItem(mimo, "tts_url");
        if (cJSON_IsString(tts_url) && tts_url->valuestring)
            mimo_tts_url_ = tts_url->valuestring;

        cJSON* tts_model = cJSON_GetObjectItem(mimo, "tts_model");
        if (cJSON_IsString(tts_model) && tts_model->valuestring)
            mimo_tts_model_ = tts_model->valuestring;

        cJSON* tts_voice = cJSON_GetObjectItem(mimo, "tts_voice");
        if (cJSON_IsString(tts_voice) && tts_voice->valuestring)
            mimo_tts_voice_ = tts_voice->valuestring;
    }

    cJSON_Delete(root);

    spdlog::info("Config loaded successfully");
    spdlog::info("  Port: {}", port_);
    spdlog::info("  Server IP: {}", server_ip_);
    spdlog::info("  API URL: {}", api_url_);
    spdlog::info("  Model: {}", model_);
    spdlog::info("  WebSocket: {}:{}{}", server_ip_, ws_port_, ws_path_);
    spdlog::info("  MIMO ASR Model: {}", mimo_asr_model_);
    spdlog::info("  MIMO TTS Model: {}", mimo_tts_model_);

    return true;
}

int Config::get_port() const { return port_; }
const std::string& Config::get_server_ip() const { return server_ip_; }
const std::string& Config::get_api_key() const { return api_key_; }
const std::string& Config::get_api_url() const { return api_url_; }
const std::string& Config::get_model() const { return model_; }
const std::string& Config::get_system_prompt() const { return system_prompt_; }
int Config::get_ws_port() const { return ws_port_; }
const std::string& Config::get_ws_path() const { return ws_path_; }
const std::string& Config::get_ws_token() const { return ws_token_; }
const std::string& Config::get_mimo_api_key() const { return mimo_api_key_; }
const std::string& Config::get_mimo_asr_url() const { return mimo_asr_url_; }
const std::string& Config::get_mimo_asr_model() const { return mimo_asr_model_; }
const std::string& Config::get_mimo_tts_url() const { return mimo_tts_url_; }
const std::string& Config::get_mimo_tts_model() const { return mimo_tts_model_; }
const std::string& Config::get_mimo_tts_voice() const { return mimo_tts_voice_; }
