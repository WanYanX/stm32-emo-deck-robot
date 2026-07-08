#pragma once

#include <string>
#include <vector>

class MimoVoiceClient {
public:
    void set_api_key(const std::string& key);
    void set_asr_url(const std::string& url);
    void set_asr_model(const std::string& model);
    void set_tts_url(const std::string& url);
    void set_tts_model(const std::string& model);
    void set_tts_voice(const std::string& voice);

    std::string recognize_wav(const std::vector<unsigned char>& wav_bytes, const std::string& language = "auto") const;
    std::vector<unsigned char> synthesize_wav(const std::string& text) const;

private:
    std::string api_key_;
    std::string asr_url_ = "https://token-plan-cn.xiaomimimo.com/v1/chat/completions";
    std::string asr_model_ = "mimo-v2.5-asr";
    std::string tts_url_ = "https://token-plan-cn.xiaomimimo.com/v1/chat/completions";
    std::string tts_model_ = "mimo-v2.5-tts";
    std::string tts_voice_ = "冰糖";

    std::string post_json(const std::string& url, const std::string& body) const;
};
