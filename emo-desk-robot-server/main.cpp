#include "config.hpp"
#include "server.hpp"
#include "llm_client.hpp"
#include "mimo_voice_client.hpp"
#include "ws_voice_server.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <clocale>

#ifdef _WIN32
#include <windows.h>
#endif

#define SPDLOG_HEADER_ONLY
#include <spdlog/spdlog.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

static Server* g_server = nullptr;

void signal_handler(int sig)
{
    spdlog::warn("Received signal {}, shutting down...", sig);
    if (g_server) {
        g_server->stop();
    }
    exit(0);
}

int main()
{
#ifdef _WIN32
    // Set console to UTF-8 for Chinese display
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    setlocale(LC_ALL, ".UTF-8");
#endif

    // Initialize spdlog: console (colored) + daily file (log/emo-desk-robot-YYYY-MM-DD.log)
    try {
        std::vector<spdlog::sink_ptr> sinks;
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_pattern("[%H:%M:%S] [%^%l%$] %v");

        auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>("log/emo-desk-robot", 0, 0, false, 30);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");

        sinks.push_back(console_sink);
        sinks.push_back(file_sink);

        auto logger = std::make_shared<spdlog::logger>("main", sinks.begin(), sinks.end());
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::trace);
        spdlog::flush_on(spdlog::level::info);
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] Failed to initialize logger: " << e.what() << std::endl;
        return 1;
    }

    spdlog::info("========================================");
    spdlog::info("  Emo-Desk-Robot Server v1.0");
    spdlog::info("========================================");

    auto& config = Config::get_instance();
    if (!config.load("config.json")) {
        spdlog::error("Failed to load config.json");
        return 1;
    }

    LlmClient llm_client;
    llm_client.set_api_key(config.get_api_key());
    llm_client.set_api_url(config.get_api_url());
    llm_client.set_model(config.get_model());
    llm_client.set_system_prompt(config.get_system_prompt());

    if (config.get_api_key().empty() || config.get_api_key() == "your-api-key-here") {
        spdlog::warn("API key not configured! Please set 'api_key' in config.json");
    }

    MimoVoiceClient mimo_voice_client;
    mimo_voice_client.set_api_key(config.get_mimo_api_key());
    mimo_voice_client.set_asr_url(config.get_mimo_asr_url());
    mimo_voice_client.set_asr_model(config.get_mimo_asr_model());
    mimo_voice_client.set_tts_url(config.get_mimo_tts_url());
    mimo_voice_client.set_tts_model(config.get_mimo_tts_model());
    mimo_voice_client.set_tts_voice(config.get_mimo_tts_voice());

    if (config.get_mimo_api_key().empty() || config.get_mimo_api_key() == "your-mimo-api-key-here") {
        spdlog::warn("MIMO API key not configured! Please set 'mimo.api_key' in config.json");
    }

    Server server(config.get_server_ip(), config.get_port());
    server.set_llm_client(&llm_client);

    WsVoiceServer ws_voice_server(config.get_server_ip(), config.get_ws_port(), config.get_ws_path(), config.get_ws_token());
    ws_voice_server.set_llm_client(&llm_client);
    ws_voice_server.set_mimo_client(&mimo_voice_client);
    ws_voice_server.set_control_sender([&server](const std::string& message) {
        server.send_control_message(message);
    });

    g_server = &server;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (!server.start()) {
        spdlog::error("Failed to start server");
        return 1;
    }

    if (!ws_voice_server.start()) {
        spdlog::error("Failed to start ESP32 WebSocket voice server");
        server.stop();
        return 1;
    }

    spdlog::info("Server is running. Press Ctrl+C to stop.");

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
