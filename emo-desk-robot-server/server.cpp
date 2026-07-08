#include "server.hpp"
#include "llm_client.hpp"
#include <algorithm>

#define SPDLOG_HEADER_ONLY
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
// Convert any encoding to UTF-8 (detects UTF-8 first, falls back to system ANSI)
static std::string to_utf8(const std::string& input)
{
    if (input.empty()) return input;
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.c_str(), -1, nullptr, 0);
    UINT src_cp = CP_UTF8;
    if (wlen <= 0) {
        src_cp = CP_ACP;
        wlen = MultiByteToWideChar(CP_ACP, 0, input.c_str(), -1, nullptr, 0);
        if (wlen <= 0) return input;
    }
    std::vector<wchar_t> wbuf(static_cast<size_t>(wlen));
    MultiByteToWideChar(src_cp, 0, input.c_str(), -1, wbuf.data(), wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), -1, nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) return input;
    std::vector<char> ubuf(static_cast<size_t>(ulen));
    WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), -1, ubuf.data(), ulen, nullptr, nullptr);
    return std::string(ubuf.data());
}
#endif

static std::string safe_chat(LlmClient* client, const std::string& message)
{
    try {
        return client->chat(message);
    } catch (const std::exception& e) {
        spdlog::error("Exception in chat(): {}", e.what());
        return std::string("[Exception] ") + e.what();
    } catch (...) {
        spdlog::error("Unknown exception in chat()");
        return "[Exception] WinHTTP crashed (try re-running as admin or check firewall)";
    }
}

Server::Server(const std::string& ip, int port)
    : ip_(ip)
    , port_(port)
    , listen_sock_(INVALID_SOCKET)
    , running_(false)
    , llm_client_(nullptr)
{
}

Server::~Server()
{
    stop();
}

void Server::set_llm_client(LlmClient* client)
{
    llm_client_ = client;
}

void Server::send_control_message(const std::string& message)
{
    broadcast(message);
}

#ifdef _WIN32
static bool init_sockets()
{
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}
static void cleanup_sockets()
{
    WSACleanup();
}
#else
static bool init_sockets() { return true; }
static void cleanup_sockets() {}
#endif

bool Server::start()
{
    if (!init_sockets()) {
        spdlog::error("Failed to initialize sockets");
        return false;
    }

    listen_sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock_ == INVALID_SOCKET) {
        spdlog::error("Socket creation failed");
        cleanup_sockets();
        return false;
    }

    int opt = 1;
    setsockopt(listen_sock_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip_.c_str());
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(listen_sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        spdlog::error("Bind failed on {}:{}", ip_, port_);
        closesocket(listen_sock_);
        cleanup_sockets();
        return false;
    }

    if (listen(listen_sock_, SOMAXCONN) != 0) {
        spdlog::error("Listen failed");
        closesocket(listen_sock_);
        cleanup_sockets();
        return false;
    }

    running_ = true;
    spdlog::info("Listening on {}:{}", ip_, port_);

    std::thread accept_thread(&Server::accept_loop, this);
    accept_thread.detach();

    return true;
}

void Server::stop()
{
    running_ = false;
    if (listen_sock_ != INVALID_SOCKET) {
        closesocket(listen_sock_);
        listen_sock_ = INVALID_SOCKET;
    }

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto sock : clients_) {
            closesocket(sock);
        }
        clients_.clear();
    }

    std::lock_guard<std::mutex> lock(threads_mutex_);
    for (auto& t : client_threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    client_threads_.clear();

    cleanup_sockets();
}

void Server::accept_loop()
{
    while (running_) {
        sockaddr_in client_addr;
#ifdef _WIN32
        int addr_len = sizeof(client_addr);
#else
        socklen_t addr_len = sizeof(client_addr);
#endif
        SOCKET client_sock = accept(listen_sock_,
                                    reinterpret_cast<sockaddr*>(&client_addr),
                                    &addr_len);

        if (client_sock == INVALID_SOCKET) {
            if (running_) {
                spdlog::error("Accept failed");
            }
            break;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        spdlog::info("New connection from {}:{}", client_ip, ntohs(client_addr.sin_port));

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clients_.insert(client_sock);
        }

        std::lock_guard<std::mutex> lock(threads_mutex_);
        client_threads_.emplace_back(&Server::handle_client, this, client_sock);
    }
}

void Server::broadcast(const std::string& message, SOCKET exclude)
{
    std::lock_guard<std::mutex> lock(clients_mutex_);
    std::string msg = message + "\n";
    auto it = clients_.begin();
    while (it != clients_.end()) {
        SOCKET sock = *it;
        if (sock != exclude) {
            int ret = send(sock, msg.c_str(), msg.length(), 0);
            if (ret == SOCKET_ERROR) {
                spdlog::warn("Broadcast: removing broken socket");
                closesocket(sock);
                it = clients_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

void Server::handle_client(SOCKET client_sock)
{
    std::string buffer;
    char temp[4096];

    while (running_) {
#ifdef _WIN32
        int timeout = 30000;
        setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#endif

#ifdef _WIN32
        int bytes = recv(client_sock, temp, sizeof(temp) - 1, 0);
#else
        ssize_t bytes = recv(client_sock, temp, sizeof(temp) - 1, 0);
#endif
        if (bytes > 0) {
            temp[bytes] = '\0';
            buffer.append(temp, static_cast<size_t>(bytes));

            size_t pos;
            while ((pos = buffer.find('\n')) != std::string::npos) {
                std::string message = buffer.substr(0, pos);
                buffer.erase(0, pos + 1);

                message.erase(0, message.find_first_not_of(" \t\r\n"));
                message.erase(message.find_last_not_of(" \t\r\n") + 1);

                if (message.empty()) continue;

                // Strip [ESP32] prefix if present
                std::string user_text = message;
                if (user_text.size() >= 7 && user_text.substr(0, 7) == "[ESP32]") {
                    user_text = user_text.substr(7);
                }

#ifdef _WIN32
                // Convert any encoding to UTF-8 before passing to LLM
                user_text = to_utf8(user_text);
                spdlog::info("Received: {}", user_text);
#else
                spdlog::info("Received: {}", user_text);
#endif

                if (llm_client_) {
                    std::string response = safe_chat(llm_client_, user_text);
#ifdef _WIN32
                    spdlog::info("LLM Response: {}", to_utf8(response));
#else
                    spdlog::info("LLM Response: {}", response);
#endif

                    std::string resp_line = response + "\n";
                    send(client_sock, resp_line.c_str(), resp_line.length(), 0);

                    broadcast(response, client_sock);
                } else {
                    std::string err = "[Error] LLM client not initialized\n";
                    send(client_sock, err.c_str(), err.length(), 0);
                }
            }
        } else if (bytes == 0) {
            spdlog::info("Client disconnected");
            break;
        } else {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) continue;
#endif
            if (running_) {
                spdlog::error("Recv error");
            }
            break;
        }
    }

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.erase(client_sock);
    }
    closesocket(client_sock);
}
