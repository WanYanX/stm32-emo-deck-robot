#include "ws_voice_server.hpp"
#include "audio_codec.hpp"
#include "llm_client.hpp"
#include "mimo_voice_client.hpp"
#include "cJSON/include/cJSON.h"
#include <libwebsockets.h>
#include <cstring>
#include <deque>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#define SPDLOG_HEADER_ONLY
#include <spdlog/spdlog.h>

struct OutMessage {
    std::vector<unsigned char> data;
    int write_type;
    int frame_duration_ms = 0;
};

static constexpr int kTtsFrameDurationMs = 60;

struct VoiceSession {
    WsVoiceServer* server = nullptr;
    std::string session_id;
    AudioCodec audio_codec;
    std::deque<OutMessage> outbox;
    std::chrono::steady_clock::time_point next_audio_send_time = std::chrono::steady_clock::now();
    std::mutex mutex;
    bool listening = false;
    unsigned long long binary_frames_received = 0;
    unsigned long long binary_frames_decoded = 0;
    unsigned long long binary_frames_failed = 0;
    unsigned long long binary_frames_ignored = 0;
    unsigned long long binary_bytes_received = 0;
    unsigned long long control_messages_received = 0;
};

static std::atomic<unsigned long long> g_session_seq{1};
static WsVoiceServer* g_active_server = nullptr;

static std::string json_get_string(cJSON* root, const char* key)
{
    cJSON* item = cJSON_GetObjectItem(root, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

static std::string json_escape_ws(const std::string& s)
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

static std::vector<std::string> extract_control_commands(const std::string& text)
{
    std::vector<std::string> commands;
    size_t pos = 0;
    while ((pos = text.find('@', pos)) != std::string::npos) {
        size_t end = text.find('#', pos + 1);
        if (end == std::string::npos)
            break;
        commands.push_back(text.substr(pos, end - pos + 1));
        pos = end + 1;
    }
    return commands;
}

static std::string remove_control_commands(const std::string& text)
{
    std::string out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t start = text.find('@', pos);
        if (start == std::string::npos) {
            out.append(text.substr(pos));
            break;
        }

        size_t end = text.find('#', start + 1);
        if (end == std::string::npos) {
            out.append(text.substr(pos));
            break;
        }

        out.append(text.substr(pos, start - pos));
        pos = end + 1;
    }

    while (!out.empty() && (out.front() == ' ' || out.front() == '\t' || out.front() == '\r' || out.front() == '\n'))
        out.erase(out.begin());
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t' || out.back() == '\r' || out.back() == '\n'))
        out.pop_back();
    return out;
}

static std::string make_session_id()
{
    return "esp32-" + std::to_string(g_session_seq.fetch_add(1));
}

static void reset_audio_debug_counters(VoiceSession* session)
{
    if (!session)
        return;

    session->binary_frames_received = 0;
    session->binary_frames_decoded = 0;
    session->binary_frames_failed = 0;
    session->binary_frames_ignored = 0;
    session->binary_bytes_received = 0;
}

static std::vector<unsigned char> with_lws_pre(const std::string& text)
{
    std::vector<unsigned char> data(LWS_PRE + text.size());
    std::copy(text.begin(), text.end(), data.begin() + LWS_PRE);
    return data;
}

static std::vector<unsigned char> with_lws_pre(const std::vector<unsigned char>& binary)
{
    std::vector<unsigned char> data(LWS_PRE + binary.size());
    std::copy(binary.begin(), binary.end(), data.begin() + LWS_PRE);
    return data;
}

static void queue_text(struct lws* wsi, VoiceSession* session, const std::string& text)
{
    std::lock_guard<std::mutex> lock(session->mutex);
    session->outbox.push_back({with_lws_pre(text), LWS_WRITE_TEXT, 0});
    lws_callback_on_writable(wsi);
}

static void queue_binary(struct lws* wsi, VoiceSession* session, const std::vector<unsigned char>& binary)
{
    if (binary.empty()) return;
    std::lock_guard<std::mutex> lock(session->mutex);
    session->outbox.push_back({with_lws_pre(binary), LWS_WRITE_BINARY, kTtsFrameDurationMs});
    lws_callback_on_writable(wsi);
}

static void process_utterance(struct lws* wsi, VoiceSession* session)
{
    WsVoiceServer* server = session->server;
    if (!server) {
        spdlog::warn("Cannot process utterance: missing server for session {}", session ? session->session_id : "<null>");
        return;
    }

    if (session->audio_codec.empty()) {
        spdlog::warn("Listen stop with empty audio [{}]: frames={}, decoded={}, failed={}, ignored={}, bytes={}",
                     session->session_id,
                     session->binary_frames_received,
                     session->binary_frames_decoded,
                     session->binary_frames_failed,
                     session->binary_frames_ignored,
                     session->binary_bytes_received);
        return;
    }

    std::string stt;
    std::vector<unsigned char> wav_bytes = session->audio_codec.build_wav();
    spdlog::info("Utterance audio summary [{}]: frames={}, decoded={}, failed={}, ignored={}, bytes={}, pcm_samples={}, wav_bytes={}",
                 session->session_id,
                 session->binary_frames_received,
                 session->binary_frames_decoded,
                 session->binary_frames_failed,
                 session->binary_frames_ignored,
                 session->binary_bytes_received,
                 session->audio_codec.pcm_sample_count(),
                 wav_bytes.size());

    if (server->mimo_client())
        stt = server->mimo_client()->recognize_wav(wav_bytes);
    if (stt.empty())
        stt = "[Error] ASR empty result";
    spdlog::info("ASR recognized [{}]: {}", session->session_id, stt);

    std::string answer = stt;
    if (server->llm_client())
        answer = server->llm_client()->chat(stt);

    spdlog::info("Answer text [{}]: {}", session->session_id, answer);
    std::string speech_text = remove_control_commands(answer);
    if (speech_text.empty())
        speech_text = "好的。";

    if (server->mimo_client()) {
        auto wav = server->mimo_client()->synthesize_wav(speech_text);
        auto opus_frames = AudioCodec::encode_wav_to_opus_frames(wav, 16000, 1, kTtsFrameDurationMs);
        spdlog::info("TTS generated [{}]: speech_chars={}, wav_bytes={}, opus_frames={}",
                     session->session_id,
                     speech_text.size(),
                     wav.size(),
                     opus_frames.size());
        for (const auto& frame : opus_frames)
            queue_binary(wsi, session, frame);
    }

    std::vector<std::string> control_commands = extract_control_commands(answer);
    if (control_commands.empty()) {
        spdlog::info("Control commands [{}]: none", session->session_id);
    } else {
        for (const auto& command : control_commands)
            spdlog::info("Control command [{}]: {}", session->session_id, command);
    }

    server->send_control_message(answer);
    session->audio_codec.clear();
}

static int voice_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len)
{
    auto** session_slot = static_cast<VoiceSession**>(user);
    VoiceSession* session = session_slot ? *session_slot : nullptr;
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            if (session_slot && !*session_slot)
                *session_slot = new VoiceSession();
            session = session_slot ? *session_slot : nullptr;
            if (!session)
                return -1;
            session->server = g_active_server;
            session->session_id = make_session_id();
            session->next_audio_send_time = std::chrono::steady_clock::now();
            reset_audio_debug_counters(session);
            spdlog::info("ESP32 WebSocket connected: {}", session->session_id);
            break;

        case LWS_CALLBACK_RECEIVE: {
            if (!session) break;
            if (lws_frame_is_binary(wsi)) {
                const auto* bytes = static_cast<const unsigned char*>(in);
                session->binary_frames_received++;
                session->binary_bytes_received += len;

                if (!session->listening) {
                    session->binary_frames_ignored++;
                    spdlog::warn("Ignored binary frame while not listening [{}]: len={}, ignored={}, total_frames={}",
                                 session->session_id,
                                 len,
                                 session->binary_frames_ignored,
                                 session->binary_frames_received);
                    break;
                }

                bool ok = session->audio_codec.decode_opus_frame(bytes, static_cast<int>(len));
                if (ok)
                    session->binary_frames_decoded++;
                else
                    session->binary_frames_failed++;

                break;
            }

            std::string text(static_cast<const char*>(in), len);
            session->control_messages_received++;
            cJSON* root = cJSON_Parse(text.c_str());
            if (!root) {
                queue_text(wsi, session, "{\"type\":\"error\",\"message\":\"invalid json\"}");
                break;
            }

            std::string type = json_get_string(root, "type");
            std::string state = json_get_string(root, "state");
            if (type == "hello") {
                spdlog::info("Received hello [{}]", session->session_id);
                std::string ack = "{\"type\":\"hello\",\"transport\":\"websocket\",\"session_id\":\"" + session->session_id + "\",\"audio_params\":{\"format\":\"opus\",\"sample_rate\":16000,\"channels\":1,\"frame_duration\":" + std::to_string(kTtsFrameDurationMs) + "}}";
                queue_text(wsi, session, ack);
            } else if (type == "listen" && state == "start") {
                session->listening = true;
                session->audio_codec.clear();
                reset_audio_debug_counters(session);
                spdlog::info("Listen start [{}]: audio buffer cleared", session->session_id);
                queue_text(wsi, session, "{\"session_id\":\"" + session->session_id + "\",\"type\":\"listen\",\"state\":\"start\"}");
            } else if (type == "listen" && state == "stop") {
                session->listening = false;
                spdlog::info("Listen stop [{}]: frames={}, decoded={}, failed={}, ignored={}, bytes={}, pcm_samples={}",
                             session->session_id,
                             session->binary_frames_received,
                             session->binary_frames_decoded,
                             session->binary_frames_failed,
                             session->binary_frames_ignored,
                             session->binary_bytes_received,
                             session->audio_codec.pcm_sample_count());
                process_utterance(wsi, session);
            } else if (type == "listen" && state == "detect") {
                spdlog::info("Listen detect [{}]", session->session_id);
                queue_text(wsi, session, "{\"session_id\":\"" + session->session_id + "\",\"type\":\"listen\",\"state\":\"detect\"}");
            } else if (type == "abort") {
                session->audio_codec.clear();
                reset_audio_debug_counters(session);
                spdlog::info("Abort [{}]: audio buffer cleared", session->session_id);
                queue_text(wsi, session, "{\"session_id\":\"" + session->session_id + "\",\"type\":\"abort\",\"state\":\"ok\"}");
            } else {
                spdlog::warn("Unknown control message [{}]: type={}, state={}, payload={}", session->session_id, type, state, text);
            }
            cJSON_Delete(root);
            break;
        }

        case LWS_CALLBACK_SERVER_WRITEABLE: {
            if (!session) break;
            std::lock_guard<std::mutex> lock(session->mutex);
            if (!session->outbox.empty()) {
                OutMessage& front = session->outbox.front();
                if (front.frame_duration_ms > 0) {
                    auto now = std::chrono::steady_clock::now();
                    if (now < session->next_audio_send_time) {
                        auto delay = std::chrono::duration_cast<std::chrono::microseconds>(session->next_audio_send_time - now).count();
                        lws_set_timer_usecs(wsi, delay > 0 ? static_cast<lws_usec_t>(delay) : 1);
                        break;
                    }
                }

                OutMessage msg = std::move(session->outbox.front());
                session->outbox.pop_front();
                lws_write(wsi, msg.data.data() + LWS_PRE, static_cast<int>(msg.data.size() - LWS_PRE), static_cast<lws_write_protocol>(msg.write_type));
                if (msg.frame_duration_ms > 0)
                    session->next_audio_send_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(msg.frame_duration_ms);
                if (!session->outbox.empty())
                    lws_callback_on_writable(wsi);
            }
            break;
        }

        case LWS_CALLBACK_TIMER:
            if (session)
                lws_callback_on_writable(wsi);
            break;

        case LWS_CALLBACK_CLOSED:
            if (session) {
                spdlog::info("ESP32 WebSocket closed: {}", session->session_id);
                delete session;
                if (session_slot)
                    *session_slot = nullptr;
            }
            break;
        default:
            break;
    }
    return 0;
}

static const struct lws_protocols protocols[] = {
    {"emorobot-v1", voice_callback, sizeof(VoiceSession*), 65536, 0, nullptr, 0},
    {nullptr, nullptr, 0, 0, 0, nullptr, 0}
};

WsVoiceServer::WsVoiceServer(const std::string& ip, int port, const std::string& path, const std::string& token)
    : ip_(ip), port_(port), path_(path), token_(token)
{
}

WsVoiceServer::~WsVoiceServer()
{
    stop();
}

void WsVoiceServer::set_llm_client(LlmClient* client) { llm_client_ = client; }
void WsVoiceServer::set_mimo_client(MimoVoiceClient* client) { mimo_client_ = client; }
void WsVoiceServer::set_control_sender(std::function<void(const std::string&)> sender) { control_sender_ = std::move(sender); }
LlmClient* WsVoiceServer::llm_client() const { return llm_client_; }
MimoVoiceClient* WsVoiceServer::mimo_client() const { return mimo_client_; }
void WsVoiceServer::send_control_message(const std::string& message) const
{
    if (control_sender_)
        control_sender_(message);
}

bool WsVoiceServer::start()
{
    if (running_) return true;
    g_active_server = this;
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = port_;
    info.iface = ip_ == "0.0.0.0" ? nullptr : ip_.c_str();
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE;

    context_ = lws_create_context(&info);
    if (!context_) {
        spdlog::error("Failed to create libwebsockets context on {}:{}", ip_, port_);
        return false;
    }
    running_ = true;
    thread_ = std::thread(&WsVoiceServer::service_loop, this);
    spdlog::info("ESP32 WebSocket voice server listening on ws://{}:{}{}", ip_, port_, path_);
    return true;
}

void WsVoiceServer::stop()
{
    running_ = false;
    if (context_)
        lws_cancel_service(static_cast<lws_context*>(context_));
    if (thread_.joinable())
        thread_.join();
    if (context_) {
        lws_context_destroy(static_cast<lws_context*>(context_));
        context_ = nullptr;
    }
}

void WsVoiceServer::service_loop()
{
    while (running_ && context_)
        lws_service(static_cast<lws_context*>(context_), 50);
}
