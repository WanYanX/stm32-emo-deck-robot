#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

class LlmClient;
class MimoVoiceClient;

class WsVoiceServer {
public:
    WsVoiceServer(const std::string& ip, int port, const std::string& path, const std::string& token);
    ~WsVoiceServer();

    void set_llm_client(LlmClient* client);
    void set_mimo_client(MimoVoiceClient* client);
    void set_control_sender(std::function<void(const std::string&)> sender);
    LlmClient* llm_client() const;
    MimoVoiceClient* mimo_client() const;
    void send_control_message(const std::string& message) const;
    bool start();
    void stop();

private:
    void service_loop();

    std::string ip_;
    int port_;
    std::string path_;
    std::string token_;
    LlmClient* llm_client_ = nullptr;
    MimoVoiceClient* mimo_client_ = nullptr;
    std::function<void(const std::string&)> control_sender_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    void* context_ = nullptr;
};
