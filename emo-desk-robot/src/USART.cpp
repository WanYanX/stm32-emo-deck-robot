// USART.cpp
#include "USART.hpp"
#include "Delay.hpp"   // 用于 Delay_ms

// 静态成员定义
USART_RxBuffer USART::rx_buffers_[static_cast<int>(USART_ID::USART_COUNT)];

// ---------- 默认引脚配置 ----------
static const USART_PinConfig DEFAULT_PIN_CONFIGS[6] = {
    // USART1: PA9(TX), PA10(RX)
    {GPIOA, GPIOA, GPIO_Pin_9, GPIO_Pin_10,
     GPIO_PinSource9, GPIO_PinSource10, GPIO_AF_USART1},
    // USART2: PA2(TX), PA3(RX)
    {GPIOA, GPIOA, GPIO_Pin_2, GPIO_Pin_3,
     GPIO_PinSource2, GPIO_PinSource3, GPIO_AF_USART2},
    // USART3: PB10(TX), PB11(RX)
    {GPIOB, GPIOB, GPIO_Pin_10, GPIO_Pin_11,
     GPIO_PinSource10, GPIO_PinSource11, GPIO_AF_USART3},
    // UART4: PA0(TX), PA1(RX)
    {GPIOA, GPIOA, GPIO_Pin_0, GPIO_Pin_1,
     GPIO_PinSource0, GPIO_PinSource1, GPIO_AF_UART4},
    // UART5: PC12(TX), PD2(RX)
    {GPIOC, GPIOD, GPIO_Pin_12, GPIO_Pin_2,
     GPIO_PinSource12, GPIO_PinSource2, GPIO_AF_UART5},
    // USART6: PC6(TX), PC7(RX)
    {GPIOC, GPIOC, GPIO_Pin_6, GPIO_Pin_7,
     GPIO_PinSource6, GPIO_PinSource7, GPIO_AF_USART6}
};

// ---------- 时钟配置 ----------
static const USART_ClockConfig DEFAULT_CLK_CONFIGS[6] = {
    // USART1 - APB2
    {RCC_AHB1Periph_GPIOA, RCC_APB2Periph_USART1, RCC_APB2PeriphClockCmd, USART1_IRQn},
    // USART2 - APB1
    {RCC_AHB1Periph_GPIOA, RCC_APB1Periph_USART2, RCC_APB1PeriphClockCmd, USART2_IRQn},
    // USART3 - APB1
    {RCC_AHB1Periph_GPIOB, RCC_APB1Periph_USART3, RCC_APB1PeriphClockCmd, USART3_IRQn},
    // UART4 - APB1
    {RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_GPIOC, RCC_APB1Periph_UART4, RCC_APB1PeriphClockCmd, UART4_IRQn},
    // UART5 - APB1
    {RCC_AHB1Periph_GPIOC | RCC_AHB1Periph_GPIOD, RCC_APB1Periph_UART5, RCC_APB1PeriphClockCmd, UART5_IRQn},
    // USART6 - APB2
    {RCC_AHB1Periph_GPIOC, RCC_APB2Periph_USART6, RCC_APB2PeriphClockCmd, USART6_IRQn}
};

static USART_TypeDef* const USART_INSTANCES[6] = {
    USART1, USART2, USART3, UART4, UART5, USART6
};

// ---------- 构造函数 ----------
USART::USART() : id_(USART_ID::USART_COUNT), usart_(nullptr),
                 gpio_tx_(nullptr), gpio_rx_(nullptr), baudrate_(0), frame_mode_(true) {}

// ---------- 初始化 ----------
bool USART::init(USART_ID id, uint32_t baudrate, bool frame_mode) {
    USART_PinConfig pin_cfg;
    USART_ClockConfig clk_cfg;
    if (!get_default_config(id, pin_cfg, clk_cfg)) return false;
    return init(id, pin_cfg, clk_cfg, baudrate, frame_mode);
}

bool USART::init(USART_ID id, const USART_PinConfig& pin_cfg,
                 const USART_ClockConfig& clk_cfg, uint32_t baudrate,
                 bool frame_mode) {
    id_ = id;
    usart_ = USART_INSTANCES[static_cast<int>(id)];
    baudrate_ = baudrate;
    frame_mode_ = frame_mode;

    // 使能时钟
    RCC_AHB1PeriphClockCmd(clk_cfg.RCC_AHB1Periph_GPIO, ENABLE);
    clk_cfg.RCC_APBxPeriphClockCmd(clk_cfg.RCC_APBxPeriph_USART, ENABLE);

    configure_gpio(pin_cfg);
    configure_usart(baudrate);
    configure_nvic(clk_cfg.IRQn);

    // 清空缓冲区并设置模式
    auto& buf = rx_buffers_[static_cast<int>(id)];
    buf.write_pos = 0;
    buf.read_pos = 0;
    buf.frame_ready = false;
    buf.status = 0;
    buf.frame_mode_enabled = frame_mode;

    USART_ITConfig(usart_, USART_IT_RXNE, ENABLE);
    USART_Cmd(usart_, ENABLE);
    return true;
}

// ---------- GPIO 配置 ----------
void USART::configure_gpio(const USART_PinConfig& cfg) {
    gpio_tx_ = cfg.GPIOx_TX;
    gpio_rx_ = cfg.GPIOx_RX;

    GPIO_InitTypeDef gpio_init;
    GPIO_StructInit(&gpio_init);

    // TX: 复用推挽
    gpio_init.GPIO_Mode  = GPIO_Mode_AF;
    gpio_init.GPIO_OType = GPIO_OType_PP;
    gpio_init.GPIO_Pin   = cfg.GPIO_Pin_TX;
    gpio_init.GPIO_Speed = GPIO_High_Speed;
    gpio_init.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(cfg.GPIOx_TX, &gpio_init);

    // RX: 复用输入
    gpio_init.GPIO_Pin = cfg.GPIO_Pin_RX;
    GPIO_Init(cfg.GPIOx_RX, &gpio_init);

    GPIO_PinAFConfig(cfg.GPIOx_TX, cfg.GPIO_PinSource_TX, cfg.GPIO_AF);
    GPIO_PinAFConfig(cfg.GPIOx_RX, cfg.GPIO_PinSource_RX, cfg.GPIO_AF);
}

// ---------- USART 参数 ----------
void USART::configure_usart(uint32_t baudrate) {
    USART_InitTypeDef usart_init;
    USART_StructInit(&usart_init);
    usart_init.USART_BaudRate            = baudrate;
    usart_init.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart_init.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    usart_init.USART_Parity              = USART_Parity_No;
    usart_init.USART_StopBits            = USART_StopBits_1;
    usart_init.USART_WordLength          = USART_WordLength_8b;
    USART_Init(usart_, &usart_init);
}

// ---------- NVIC ----------
void USART::configure_nvic(IRQn_Type IRQn) {
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    NVIC_InitTypeDef nvic;
    nvic.NVIC_IRQChannel = IRQn;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority = 1;
    NVIC_Init(&nvic);
}

// ---------- 获取默认配置 ----------
bool USART::get_default_config(USART_ID id, USART_PinConfig& pin, USART_ClockConfig& clk) {
    int idx = static_cast<int>(id);
    if (idx < 0 || idx >= static_cast<int>(USART_ID::USART_COUNT)) return false;
    pin = DEFAULT_PIN_CONFIGS[idx];
    clk = DEFAULT_CLK_CONFIGS[idx];
    return true;
}

uint32_t USART::get_apb_clock(USART_ID id) {
    RCC_ClocksTypeDef clocks;
    RCC_GetClocksFreq(&clocks);
    if (id == USART_ID::USART_1 || id == USART_ID::USART_6)
        return clocks.PCLK2_Frequency;
    else
        return clocks.PCLK1_Frequency;
}

// ---------- 发送函数 ----------
void USART::send_byte(uint8_t data) {
    if (!usart_) return;
    USART_SendData(usart_, data);
    while (USART_GetFlagStatus(usart_, USART_FLAG_TXE) == RESET);
}

void USART::send_data(const char* data) {
    if (!usart_ || !data) return;
    for (int i = 0; data[i] != STOP_FLAG; ++i)
        send_byte(static_cast<uint8_t>(data[i]));
}

void USART::send_data(const char* data, uint16_t len) {
    if (!usart_ || !data) return;
    for (uint16_t i = 0; i < len; ++i)
        send_byte(static_cast<uint8_t>(data[i]));
}

void USART::send_string(const std::string& str) {
    send_data(str.c_str(), static_cast<uint16_t>(str.length()));
    send_byte('\r');
    send_byte('\n');
}

// ---------- 接收（帧模式） ----------
bool USART::has_frame() const {
    if (id_ == USART_ID::USART_COUNT) return false;
    return rx_buffers_[static_cast<int>(id_)].frame_ready;
}

bool USART::has_data() const {
    if (id_ == USART_ID::USART_COUNT) return false;
    auto& buf = rx_buffers_[static_cast<int>(id_)];
    return buf.read_pos != buf.write_pos;
}

std::string USART::read_data() {
    if (!has_data()) return "";
    auto& buf = rx_buffers_[static_cast<int>(id_)];
    std::string result;
    while (buf.read_pos != buf.write_pos) {
        result += buf.buffer[buf.read_pos];
        buf.read_pos = (buf.read_pos + 1) % USART_RxBuffer::MAX_CMD_LEN;
    }
    return result;
}

std::string USART::receive_frame() {
    if (id_ == USART_ID::USART_COUNT) return "";
    auto& buf = rx_buffers_[static_cast<int>(id_)];
    if (!buf.frame_ready) return "";

    // 提取帧内容（不含@和#）
    std::string result;
    uint16_t start = (buf.frame_start + 1) % USART_RxBuffer::MAX_CMD_LEN;
    uint16_t end = buf.frame_end;   // 指向#之后的位置

    if (end < start) {
        for (uint16_t i = start; i < USART_RxBuffer::MAX_CMD_LEN; ++i)
            result += buf.buffer[i];
        for (uint16_t i = 0; i < end; ++i)
            result += buf.buffer[i];
    } else {
        for (uint16_t i = start; i < end; ++i)
            result += buf.buffer[i];
    }

    // 清除帧标志
    buf.frame_ready = false;
    buf.status = 0;
    // 将读指针移到帧结束位置，避免重复读取
    buf.read_pos = end;
    return result;
}

// ---------- 接收（原始模式） ----------
std::string USART::read_raw(uint32_t timeout_ms) {
    if (id_ == USART_ID::USART_COUNT) return "";
    auto& buf = rx_buffers_[static_cast<int>(id_)];
    uint32_t elapsed = 0;
    std::string result;

    while (elapsed < timeout_ms) {
        if (buf.read_pos != buf.write_pos) {
            // 读取当前所有可读数据并追加到 result
            while (buf.read_pos != buf.write_pos) {
                result += buf.buffer[buf.read_pos];
                buf.read_pos = (buf.read_pos + 1) % USART_RxBuffer::MAX_CMD_LEN;
            }
            // 注意：不立即返回，继续等待可能后续到达的数据
        }
        Delay_ms(1);
        ++elapsed;
    }
    return result;   // 超时后返回累积的全部数据
}

std::string USART::read_until_timeout(uint32_t timeout_ms) {
    return read_raw(timeout_ms);
}

void USART::clear_rx() {
    if (id_ == USART_ID::USART_COUNT) return;
    auto& buf = rx_buffers_[static_cast<int>(id_)];
    buf.write_pos = 0;
    buf.read_pos = 0;
    buf.frame_ready = false;
    buf.status = 0;
    buf.frame_start = 0;
    buf.frame_end = 0;
}

// ---------- 辅助 ----------
USART_ID USART::get_id_from_instance(USART_TypeDef* usart) {
    for (int i = 0; i < static_cast<int>(USART_ID::USART_COUNT); ++i) {
        if (USART_INSTANCES[i] == usart) return static_cast<USART_ID>(i);
    }
    return USART_ID::USART_COUNT;
}

// ============================================================
// 中断处理核心（双模式支持）
// ============================================================
void USART_IRQHandler_Impl(USART_TypeDef* usart, USART_ID id) {
    if (USART_GetITStatus(usart, USART_IT_RXNE) == RESET) return;

    uint8_t data = USART_ReceiveData(usart);
    auto& buf = USART::rx_buffers_[static_cast<int>(id)];

    // 不管什么模式，先把数据存入环形缓冲区
    buf.buffer[buf.write_pos] = static_cast<char>(data);
    uint16_t next_pos = (buf.write_pos + 1) % USART_RxBuffer::MAX_CMD_LEN;
    // 如果写指针追上读指针（缓冲区满），则丢弃最老的数据（覆盖）
    if (next_pos == buf.read_pos) {
        // 读指针前进一位（丢弃一个字节）
        buf.read_pos = (buf.read_pos + 1) % USART_RxBuffer::MAX_CMD_LEN;
        // 如果帧模式，可能破坏帧，简单处理：丢弃帧状态
        if (buf.frame_mode_enabled && buf.frame_ready) {
            buf.frame_ready = false;
            buf.status = 0;
        }
    }
    buf.write_pos = next_pos;

    // 如果启用帧模式，进行帧解析
    if (buf.frame_mode_enabled) {
        switch (buf.status) {
            case 0: // 等待帧头 '@'
                if (data == USART::START_FLAG) {
                    buf.status = 1;
                    buf.frame_start = (buf.write_pos - 1 + USART_RxBuffer::MAX_CMD_LEN) % USART_RxBuffer::MAX_CMD_LEN;
                    // 记录@的位置，但@本身不存入有效帧，实际数据从@之后开始
                }
                break;
            case 1: // 接收数据
                if (data == USART::START_FLAG) {
                    // 重新开始帧
                    buf.frame_start = (buf.write_pos - 1 + USART_RxBuffer::MAX_CMD_LEN) % USART_RxBuffer::MAX_CMD_LEN;
                } else if (data == USART::END_FLAG) {
                    // 帧结束，记录结束位置（不包含#）
                    buf.frame_end = (buf.write_pos - 1 + USART_RxBuffer::MAX_CMD_LEN) % USART_RxBuffer::MAX_CMD_LEN;
                    // 检查是否非空帧（数据长度>0）
                    if (buf.frame_end != buf.frame_start) {
                        buf.frame_ready = true;
                    } else {
                        // 空帧丢弃
                        buf.status = 0;
                    }
                }
                // 其他数据继续接收，不做特殊处理
                break;
        }
        // 注意：帧结束时不重置status，留待用户读取后重置，但为了能继续接收下一帧，在frame_ready后保留状态，直到receive_frame被调用
        // 也可以立即重置status，但receive_frame会重置，此处不重置，以便下次收到@时重置
        // 我们选择在receive_frame中重置status
    }

    USART_ClearITPendingBit(usart, USART_IT_RXNE);
}

// C 中断入口
extern "C" {
void USART1_IRQHandler(void) { USART_IRQHandler_Impl(USART1, USART_ID::USART_1); }
void USART2_IRQHandler(void) { USART_IRQHandler_Impl(USART2, USART_ID::USART_2); }
void USART3_IRQHandler(void) { USART_IRQHandler_Impl(USART3, USART_ID::USART_3); }
void UART4_IRQHandler(void)  { USART_IRQHandler_Impl(UART4, USART_ID::UART_4); }
void UART5_IRQHandler(void)  { USART_IRQHandler_Impl(UART5, USART_ID::UART_5); }
void USART6_IRQHandler(void) { USART_IRQHandler_Impl(USART6, USART_ID::USART_6); }
}