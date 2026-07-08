#include "app.hpp"
#include <cstdlib>
#include <cstring>

static char ipd_buf[2048];
static int  ipd_len       = 0;
static const int IPD_BUF_MAX = sizeof(ipd_buf) - 1;

static void handle_ipd_line(const char *line)
{
    if (!line) return;

    const char *ipd = strstr(line, "+IPD,");
    if (!ipd) return;

    const char *colon = strchr(ipd, ':');
    if (!colon) return;

    const char *data = colon + 1;
    size_t dlen      = strlen(data);
    while (dlen > 0 && (data[dlen - 1] == '\r' || data[dlen - 1] == '\n')) {
        dlen--;
    }

    if (dlen > 0) {
        std::string msg(data, dlen);

        /* 解析服务端回复：@命令#文字 */
        if (msg.size() >= 3 && msg.front() == '@') {
            size_t hash = msg.find('#');
            if (hash != std::string::npos) {
                std::string cmd = msg.substr(1, hash - 1);
                CmdManager::instance().execute(cmd);

                /* # 后面是显示文字，转发给 OLED */
                std::string display = msg.substr(hash + 1);
                if (!display.empty()) {
                    App::get_instance().output_llm("[LLM] " + display);
                }
                return;
            }
        }

        /* 没有命令，整条消息作为 LLM 回复显示 */
        App::get_instance().output_llm("[LLM] " + msg);
    }
}

App &App::get_instance()
{
    static App app;
    return app;
}

void App::output_llm(const std::string &msg)
{
    usart_1_.send_string(msg);
}

int App::run()
{
    AnimationManager::instance().boot_to_idle();

    while (1) {
        /* USART3 — ESP8266 数据 */
        if (usart_3_.has_data()) {
            std::string raw = usart_3_.read_data();

            for (size_t i = 0; i < raw.length() && ipd_len < IPD_BUF_MAX; i++) {
                char c = raw[i];
                if (c == '\n') {
                    ipd_buf[ipd_len] = '\0';
                    handle_ipd_line(ipd_buf);
                    ipd_len = 0;
                } else {
                    ipd_buf[ipd_len++] = c;
                }
            }

            if (ipd_len >= IPD_BUF_MAX) {
                int half = IPD_BUF_MAX / 2;
                memmove(ipd_buf, ipd_buf + half, IPD_BUF_MAX - half);
                ipd_len = IPD_BUF_MAX - half;
            }
        }

        /* USART1 — 调试串口命令 */
        if (usart_1_.has_frame()) {
            std::string cmd = usart_1_.receive_frame();
            CmdManager::instance().execute(cmd);
        }

        AnimationManager::instance().tick();

        if (oled_.needsUpdate()) {
            oled_.update();
        }

        Delay_ms(6);
    }

    return 0;
}

void App::init_usart1()
{
    usart_1_.init(USART_ID::USART_1, ResourceManager::USART1_BAUD, true);
    usart_1_.send_string("USART 1 init");
}

void App::init_oled()
{
    oled_.init();
    usart_1_.send_string("1.3-oled init");
}

void App::init_servo()
{
    const auto& hw = ResourceManager::instance();

    Servo servo_body;
    servo_body.init(
        hw.SERVO_BODY.pin,
        RCC_AHB1Periph_GPIOD,
        RCC_APB1Periph_TIM4,
        hw.SERVO_BODY.port,
        hw.SERVO_BODY.pinSource,
        hw.SERVO_BODY.af,
        hw.SERVO_BODY.tim,
        hw.SERVO_BODY.oc
    );
    ServoManager::instance().register_servo("body", servo_body);

    Servo servo_head;
    servo_head.init(
        hw.SERVO_HEAD.pin,
        RCC_AHB1Periph_GPIOD,
        RCC_APB1Periph_TIM4,
        hw.SERVO_HEAD.port,
        hw.SERVO_HEAD.pinSource,
        hw.SERVO_HEAD.af,
        hw.SERVO_HEAD.tim,
        hw.SERVO_HEAD.oc
    );
    ServoManager::instance().register_servo("head", servo_head);
    usart_1_.send_string("ServoManager init");
}

void App::init_wifi()
{
    usart_3_.init(USART_ID::USART_3, ResourceManager::USART3_BAUD, false);

    const auto& hw = ResourceManager::instance();

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOF | RCC_AHB1Periph_GPIOC, ENABLE);
    bool esp_ok = esp8266_.init(usart_3_,
                                hw.ESP_RST_GPIO, hw.ESP_RST_PIN,
                                hw.ESP_IO0_GPIO, hw.ESP_IO0_PIN);

    if (esp_ok) {
        usart_1_.send_string("ESP8266 init OK");
        std::string version;
        if (esp8266_.get_version(version)) {
            usart_1_.send_string("Version: " + version);
        }

        if (esp8266_.set_wifi_mode(1)) {
            usart_1_.send_string("WiFi mode set to Station");
        } else {
            usart_1_.send_string("Set WiFi mode failed");
        }

        if (esp8266_.connect_ap(hw.WIFI_NAME, hw.WIFI_PSD)) {
            usart_1_.send_string("Connected to AP");
        } else {
            usart_1_.send_string("Connect AP failed");
        }
        std::string ip;
        if (esp8266_.get_station_ip(ip)) {
            usart_1_.send_string("IP: " + ip);
        }

        std::string resp;
        if (esp8266_.execute("AT+CIPMUX=0", resp, "OK", 2000)) {
            usart_1_.send_string("CIPMUX set to 0 OK");
        } else {
            usart_1_.send_string("CIPMUX set failed");
        }

        if (esp8266_.connect_tcp(hw.SERVER_IP, hw.SERVER_PORT)) {
            usart_1_.send_string("Connected to LLM server: " + std::string(hw.SERVER_IP) + ":" + std::to_string(hw.SERVER_PORT));
        } else {
            usart_1_.send_string("Connect to LLM server FAILED");
        }
    } else {
        usart_1_.send_string("ESP8266 init FAILED");
    }
}

void App::init_led()
{
    const auto& hw = ResourceManager::instance();
    led_0_.init(hw.LED_GPIO, hw.LED_PIN, ActiveLevel::LOW,
                hw.LED_RCC_GPIO, hw.LED_RCC_TIM);
    usart_1_.send_string("led_0 init");
}

void App::init()
{
    SystemInit();
    Delay_ms(200);

    init_usart1();
    init_servo();
    init_led();
    init_oled();
    init_wifi();

    AnimationManager::instance().init(oled_);

    CmdManager::instance().init(led_0_);

    RCC_ClocksTypeDef RCC_Clocks;
    RCC_GetClocksFreq(&RCC_Clocks);
    usart_1_.send_string(std::string("APB1:") + std::to_string(RCC_Clocks.PCLK1_Frequency));
    usart_1_.send_string(std::string("system-clk:") + std::to_string(RCC_Clocks.SYSCLK_Frequency));
}
