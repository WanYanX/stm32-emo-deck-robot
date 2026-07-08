#include "LED.hpp"

void LED::init(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin_x, ActiveLevel active_level,
               uint32_t RCC_AHB1Periph_GPIOx, uint32_t RCC_APB1Periph_TIMx,
               GPIOMode_TypeDef mode, GPIOOType_TypeDef otype,
               GPIOSpeed_TypeDef speed, GPIOPuPd_TypeDef pupd)
{
    // 时钟
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOx, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIMx, ENABLE);
    init_pin_.GPIO_Mode  = mode;
    init_pin_.GPIO_Pin   = GPIO_Pin_x;
    init_pin_.GPIO_PuPd  = pupd;
    init_pin_.GPIO_Speed = speed;
    init_pin_.GPIO_OType = otype;
    gpio_x_              = GPIOx;
    active_level_        = active_level;
    GPIO_Init(gpio_x_, &init_pin_);
}

void LED::turn_on()
{
    if (active_level_ == ActiveLevel::HIGH)
        GPIO_SetBits(gpio_x_, init_pin_.GPIO_Pin);
    else
        GPIO_ResetBits(gpio_x_, init_pin_.GPIO_Pin);
}

void LED::turn_off()
{
    if (active_level_ == ActiveLevel::HIGH)
        GPIO_ResetBits(gpio_x_, init_pin_.GPIO_Pin);
    else
        GPIO_SetBits(gpio_x_, init_pin_.GPIO_Pin);
}