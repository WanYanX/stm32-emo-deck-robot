#ifndef SERVO_H
#define SERVO_H

#include "Pwm.hpp"

// 仅支持有角度控制的舵机
class Servo
{

public:
    static constexpr uint16_t SERVO_MIN_PULSE = 500;
    static constexpr uint16_t SERVO_PULSE_RANGE = 2000;
    static constexpr uint8_t  SERVO_ANGLE_MAX = 180;

    Servo();
    ~Servo() = default;

    void init(uint32_t Pin_N, uint32_t RCC_AHB1Periph_GPIO_N,
              uint32_t RCC_APB1Periph_TIM_N, GPIO_TypeDef *GPIO_N,
              uint8_t GPIO_PinSource_N, uint8_t GPIO_AF_TIM_N,
              TIM_TypeDef *TIM_N, uint8_t OC_Select);

    void set_angle(uint32_t angle);
    const uint32_t get_crr() const;
private:
    Pwm p_;
    TIM_TypeDef *tim_n_;
    uint8_t oc_select_;
    uint16_t arr_;
    uint16_t psc_;
    uint8_t cnt_;
    uint32_t crr_;
};

#endif