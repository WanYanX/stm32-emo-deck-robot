#ifndef _PWM_H_
#define _PWM_H_

#include "stm32f4xx.h"

class Pwm
{
public:

    //默认是1kHz的
    void init(
        uint32_t Pin_N,uint32_t RCC_AHB1Periph_GPIO_N,
        uint32_t RCC_APB1Periph_TIM_N,GPIO_TypeDef *GPIO_N,
        uint8_t GPIO_PinSource_N,uint8_t GPIO_AF_TIM_N,
        TIM_TypeDef *TIM_N,uint8_t OC_Select,
        uint32_t arr = 100,uint32_t psc = 420,
        uint32_t cnt = 0,uint32_t crr = 0);

    Pwm() = default;
    ~Pwm() = default;
};

#endif