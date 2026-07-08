#include "Servo.hpp"

Servo::Servo()
    :arr_(20000)
    ,psc_(84)
    ,cnt_(0)
    ,crr_(1500)
{
}

void Servo::init(uint32_t Pin_N, uint32_t RCC_AHB1Periph_GPIO_N, uint32_t RCC_APB1Periph_TIM_N, GPIO_TypeDef *GPIO_N, uint8_t GPIO_PinSource_N, uint8_t GPIO_AF_TIM_N, TIM_TypeDef *TIM_N, uint8_t OC_Select)
{
    oc_select_ = OC_Select;
    tim_n_ = TIM_N;
    p_.init(
        Pin_N,RCC_AHB1Periph_GPIO_N,RCC_APB1Periph_TIM_N,
        GPIO_N,GPIO_PinSource_N,GPIO_AF_TIM_N,
        tim_n_,oc_select_,
        arr_,psc_,cnt_,crr_);

}

void Servo::set_angle(uint32_t angle)
{
    if (angle > SERVO_ANGLE_MAX) angle = SERVO_ANGLE_MAX;

    crr_ = SERVO_MIN_PULSE + angle * SERVO_PULSE_RANGE / SERVO_ANGLE_MAX;
    if (oc_select_ == 1) {
        TIM_SetCompare1(tim_n_, crr_);
    } else if (oc_select_ == 2) {
        TIM_SetCompare2(tim_n_, crr_);
    } else if (oc_select_ == 3) {
        TIM_SetCompare3(tim_n_, crr_);
    } else if (oc_select_ == 4) {
        TIM_SetCompare4(tim_n_, crr_);
    }
}

const uint32_t Servo::get_crr() const
{
    return crr_;
}
