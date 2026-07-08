#include "ESP8266.hpp"
#include "Delay.hpp"

bool ESP8266::init(USART &usart, GPIO_TypeDef *rst_port, uint16_t rst_pin,
                   GPIO_TypeDef *io0_port, uint16_t io0_pin)
{
    usart_    = &usart;
    rst_port_ = rst_port;
    rst_pin_  = rst_pin;
    io0_port_ = io0_port;
    io0_pin_  = io0_pin;

    // 配置 RST 和 IO0 为推挽输出
    GPIO_InitTypeDef gpio_init;
    gpio_init.GPIO_Mode  = GPIO_Mode_OUT;
    gpio_init.GPIO_OType = GPIO_OType_PP;
    gpio_init.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;

    gpio_init.GPIO_Pin = rst_pin_;
    GPIO_Init(rst_port_, &gpio_init);

    gpio_init.GPIO_Pin = io0_pin_;
    GPIO_Init(io0_port_, &gpio_init);

    // IO0 拉高（正常模式）
    GPIO_SetBits(io0_port_, io0_pin_);

    // 复位并测试
    reset();
    std::string resp;
    return execute("AT", resp, "OK");
}

void ESP8266::reset()
{
    if (!rst_port_) return;
    GPIO_ResetBits(rst_port_, rst_pin_);
    Delay_ms(200);
    GPIO_SetBits(rst_port_, rst_pin_);
    Delay_ms(1000); // 等待模块启动
}

void ESP8266::send_raw_cmd(const std::string &cmd)
{
    if (usart_) usart_->send_string(cmd); // send_string 自动追加 \r\n
}

bool ESP8266::execute(const std::string &cmd, std::string &response,
                      const std::string &expected, uint32_t timeout_ms)
{
    if (!usart_) return false;
    usart_->clear_rx(); // 清空旧数据
    send_raw_cmd(cmd);  // 发送指令
    response = usart_->read_until_timeout(timeout_ms);
    return response.find(expected) != std::string::npos;
}

// 获取当前第一个 TCP 连接的本地端口（适用于单连接模式）
bool ESP8266::get_local_port(uint16_t &port)
{
    std::string resp;
    if (!execute("AT+CIPSTATUS", resp, "OK", 3000)) {
        return false;
    }

    // 解析响应格式（示例）：
    // +CIPSTATUS:0,"TCP","192.168.1.100",8080,49153,0
    // 其中第5个字段（49153）就是本地端口号
    size_t start = resp.find("+CIPSTATUS:");
    if (start == std::string::npos) return false;

    // 找第4个逗号和第5个逗号之间的数字（本地端口）
    int comma_count         = 0;
    size_t pos              = start;
    size_t local_port_start = 0, local_port_end = 0;

    while (pos < resp.length()) {
        if (resp[pos] == ',') {
            comma_count++;
            if (comma_count == 4) {
                local_port_start = pos + 1; // 第5个字段开始
            }
            if (comma_count == 5) {
                local_port_end = pos; // 第5个字段结束（遇到第6个逗号）
                break;
            }
        }
        pos++;
    }

    if (local_port_start != 0 && local_port_end != 0) {
        std::string port_str = resp.substr(local_port_start, local_port_end - local_port_start);
        port                 = static_cast<uint16_t>(std::stoi(port_str));
        return true;
    }
    return false;
}

// ---------- 便捷函数实现 ----------
bool ESP8266::get_version(std::string &version)
{
    std::string resp;
    bool ok = execute("AT+GMR", resp, "OK", 3000);
    if (ok) version = resp;
    return ok;
}

bool ESP8266::set_wifi_mode(uint8_t mode)
{
    std::string resp;
    return execute("AT+CWMODE=" + std::to_string(mode), resp, "OK");
}

bool ESP8266::connect_ap(const std::string &ssid, const std::string &pwd)
{
    std::string resp;
    std::string cmd = "AT+CWJAP=\"" + ssid + "\",\"" + pwd + "\"";
    return execute(cmd, resp, "OK", 10000);
}

bool ESP8266::get_station_ip(std::string &ip)
{
    std::string resp;
    if (!execute("AT+CIPSTA?", resp, "OK", 3000)) return false;
    size_t start = resp.find("ip:\"");
    if (start != std::string::npos) {
        start += 4;
        size_t end = resp.find('"', start);
        if (end != std::string::npos) {
            ip = resp.substr(start, end - start);
            return true;
        }
    }
    return false;
}

bool ESP8266::connect_tcp(const std::string &ip, uint16_t port)
{
    std::string resp;
    std::string cmd = "AT+CIPSTART=\"TCP\",\"" + ip + "\"," + std::to_string(port);
    return execute(cmd, resp, "CONNECT", 5000);
}

bool ESP8266::send_data(const std::string &data)
{
    std::string resp;
    std::string len_cmd = "AT+CIPSEND=" + std::to_string(data.length());
    if (!execute(len_cmd, resp, ">", 2000)) return false;
    // 发送数据（不含 \r\n）
    if (usart_) {
        usart_->send_data(data.c_str(), static_cast<uint16_t>(data.length())); // 需添加 send_data 重载
    }
    resp = usart_->read_until_timeout(3000);
    return resp.find("SEND OK") != std::string::npos;
}

bool ESP8266::close_connection()
{
    std::string resp;
    return execute("AT+CIPCLOSE", resp, "OK");
}