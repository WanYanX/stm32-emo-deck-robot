#pragma once

#include <string>

class Config {
public:
    static Config& get_instance();

    bool load(const std::string& path);

    int get_port() const;
    const std::string& get_server_ip() const;
    const std::string& get_api_key() const;
    const std::string& get_api_url() const;
    const std::string& get_model() const;
    const std::string& get_system_prompt() const;
    int get_ws_port() const;
    const std::string& get_ws_path() const;
    const std::string& get_ws_token() const;
    const std::string& get_mimo_api_key() const;
    const std::string& get_mimo_asr_url() const;
    const std::string& get_mimo_asr_model() const;
    const std::string& get_mimo_tts_url() const;
    const std::string& get_mimo_tts_model() const;
    const std::string& get_mimo_tts_voice() const;

private:
    Config() = default;

    int port_ = 8288;
    std::string server_ip_ = "0.0.0.0";
    std::string api_key_;
    std::string api_url_;
    std::string model_;
    std::string system_prompt_;
    int ws_port_ = 8289;
    std::string ws_path_ = "/emorobot/v1/";
    std::string ws_token_;
    std::string mimo_api_key_;
    std::string mimo_asr_url_ = "https://api.xiaomimimo.com/v1/chat/completions";
    std::string mimo_asr_model_ = "mimo-v2.5-asr";
    std::string mimo_tts_url_ = "https://token-plan-cn.xiaomimimo.com/v1/chat/completions";
    std::string mimo_tts_model_ = "mimo-v2.5-tts";
    std::string mimo_tts_voice_ = "冰糖";
};
