#pragma once

#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <set>
#include <atomic>

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket(fd) ::close(fd)
#endif

class LlmClient;

class Server {
public:
    explicit Server(const std::string& ip, int port);
    ~Server();

    bool start();
    void stop();
    void set_llm_client(LlmClient* client);
    void send_control_message(const std::string& message);

private:
    void accept_loop();
    void handle_client(SOCKET client_sock);
    void broadcast(const std::string& message, SOCKET exclude = INVALID_SOCKET);

    std::string ip_;
    int port_;
    SOCKET listen_sock_;
    std::atomic<bool> running_;

    LlmClient* llm_client_;

    std::set<SOCKET> clients_;
    std::mutex clients_mutex_;
    std::mutex threads_mutex_;
    std::vector<std::thread> client_threads_;
};
