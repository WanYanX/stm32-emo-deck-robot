// USART.hpp
#ifndef USART_HPP
#define USART_HPP

#include <string>
#include <cstdint>
#include "stm32f4xx.h"

enum class USART_ID {
    USART_1,
    USART_2,
    USART_3,
    UART_4,
    UART_5,
    USART_6,
    USART_COUNT
};

struct USART_PinConfig {
    GPIO_TypeDef* GPIOx_TX;
    GPIO_TypeDef* GPIOx_RX;
    uint16_t GPIO_Pin_TX;
    uint16_t GPIO_Pin_RX;
    uint8_t GPIO_PinSource_TX;
    uint8_t GPIO_PinSource_RX;
    uint8_t GPIO_AF;
};

struct USART_ClockConfig {
    uint32_t RCC_AHB1Periph_GPIO;
    uint32_t RCC_APBxPeriph_USART;
    void (*RCC_APBxPeriphClockCmd)(uint32_t, FunctionalState);
    IRQn_Type IRQn;
};

// 接收缓冲区（每个串口独立）
struct USART_RxBuffer {
    static constexpr uint16_t MAX_CMD_LEN = 1024;

    volatile char buffer[MAX_CMD_LEN];
    volatile uint16_t write_pos = 0;      // 写入位置（中断中更新）
    volatile uint16_t read_pos = 0;       // 读取位置（用户读原始数据时更新）

    // 帧模式相关
    volatile bool frame_mode_enabled = true;
    volatile bool frame_ready = false;
    volatile uint16_t frame_start = 0;    // '@' 的位置
    volatile uint16_t frame_end = 0;      // '#' 之后的位置（不包含#）
    volatile uint8_t status = 0;          // 0=等待@, 1=接收数据中
};

class USART {
    friend void USART_IRQHandler_Impl(USART_TypeDef* usart, USART_ID id);
public:
    static constexpr char START_FLAG = '@';
    static constexpr char END_FLAG   = '#';
    static constexpr char STOP_FLAG  = '\0';
    static constexpr uint32_t DEFAULT_BAUDRATE = 115200;

    USART();
    ~USART() = default;

    // 初始化，frame_mode = true 启用 @# 帧解析，false 为原始模式
    bool init(USART_ID id, uint32_t baudrate = DEFAULT_BAUDRATE, bool frame_mode = true);
    bool init(USART_ID id, const USART_PinConfig& pin_cfg,
              const USART_ClockConfig& clk_cfg, uint32_t baudrate = DEFAULT_BAUDRATE,
              bool frame_mode = true);

    // 发送
    void send_byte(uint8_t data);
    void send_data(const char* data);
    void send_data(const char* data, uint16_t len);          // 发送指定长度
    void send_string(const std::string& str);
    bool has_data() const;
std::string read_data();

    // 接收（帧模式）
    std::string receive_frame();        // 返回一帧内容（不含@#），非阻塞
    bool has_frame() const;

    // 接收（原始模式）
    std::string read_raw(uint32_t timeout_ms);               // 阻塞读取所有可用数据，超时返回已读部分
    std::string read_until_timeout(uint32_t timeout_ms);    // 同 read_raw，用于 ESP8266

    void clear_rx();                     // 清空缓冲区

    USART_TypeDef* get_instance() const { return usart_; }
    static USART_ID get_id_from_instance(USART_TypeDef* usart);

private:
    USART_ID id_;
    USART_TypeDef* usart_;
    GPIO_TypeDef* gpio_tx_;
    GPIO_TypeDef* gpio_rx_;
    uint32_t baudrate_;
    bool frame_mode_;

    static USART_RxBuffer rx_buffers_[static_cast<int>(USART_ID::USART_COUNT)];

    void configure_gpio(const USART_PinConfig& cfg);
    void configure_usart(uint32_t baudrate);
    void configure_nvic(IRQn_Type IRQn);
    static bool get_default_config(USART_ID id, USART_PinConfig& pin, USART_ClockConfig& clk);
    static uint32_t get_apb_clock(USART_ID id);
};

extern "C" {
    void USART1_IRQHandler(void);
    void USART2_IRQHandler(void);
    void USART3_IRQHandler(void);
    void UART4_IRQHandler(void);
    void UART5_IRQHandler(void);
    void USART6_IRQHandler(void);
}

#endif // USART_HPP