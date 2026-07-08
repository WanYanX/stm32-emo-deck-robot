#ifndef ESP8266_HPP
#define ESP8266_HPP

#include "USART.hpp"
#include <string>

class ESP8266 {
public:
    ESP8266() = default;

    // 初始化：传入 USART 引用（必须已初始化）和引脚
    bool init(USART &usart, GPIO_TypeDef* rst_port, uint16_t rst_pin,
              GPIO_TypeDef* io0_port, uint16_t io0_pin);

    // 硬件复位（使用已配置的引脚）
    void reset();

    // 执行 AT 命令（不带 \r\n），返回完整响应和是否成功（含 expected 字符串）
    bool execute(const std::string &cmd, std::string &response,
                 const std::string &expected = "OK", uint32_t timeout_ms = 2000);

    // ---------- 便捷函数（可选） ----------
    bool get_version(std::string &version);
    bool set_wifi_mode(uint8_t mode);
    bool connect_ap(const std::string &ssid, const std::string &pwd);
    bool get_station_ip(std::string &ip);
    bool connect_tcp(const std::string &ip, uint16_t port);
    bool send_data(const std::string &data);
    bool close_connection();
    bool get_local_port(uint16_t &port);

private:
    USART* usart_ = nullptr;                 // 使用指针，在 init 中赋值
    GPIO_TypeDef* rst_port_ = nullptr;
    uint16_t rst_pin_ = 0;
    GPIO_TypeDef* io0_port_ = nullptr;
    uint16_t io0_pin_ = 0;

    // 发送原始命令（自动追加 \r\n）
    void send_raw_cmd(const std::string &cmd);
};

#endif