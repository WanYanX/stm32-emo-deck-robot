#ifndef APP_H
#define APP_H

#include "stm32f4xx.h"
#include "stm32f4xx_rcc.h"

#include "Delay.hpp"

#include "USART.hpp"
#include "LED.hpp"
#include "Servo.hpp"
#include "OLED.hpp"
#include "ESP8266.hpp"

#include "ServoManager.hpp"
#include "AnimationManager.hpp"
#include "CmdManager.hpp"
#include "ResourceManager.hpp"

class App
{
public:
    void init();
    int run();
    void output_llm(const std::string& msg);

private:
    void init_usart1();
    void init_oled();
    void init_servo();
    void init_wifi();
    void init_led();

private:
    USART usart_1_;
    USART usart_2_;
    USART usart_3_;
    LED led_0_;
    OLED oled_;
    ESP8266 esp8266_;

public:
    static App &get_instance();

private:
    App() = default;
    App(const App &) = delete;
    const App &operator=(const App &) = delete;
    ~App() = default;
};

#endif
