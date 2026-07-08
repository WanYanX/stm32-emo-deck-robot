#include "Pwm.hpp"

void Pwm::init(
    uint32_t Pin_N, uint32_t RCC_AHB1Periph_GPIO_N,
    uint32_t RCC_APB1Periph_TIM_N, GPIO_TypeDef *GPIO_N,
    uint8_t GPIO_PinSource_N, uint8_t GPIO_AF_TIM_N,
    TIM_TypeDef *TIM_N, uint8_t OC_Select,
    uint32_t arr, uint32_t psc, uint32_t cnt, uint32_t crr)
{
    GPIO_InitTypeDef init_pin_;
    init_pin_.GPIO_Mode  = GPIO_Mode_AF;
    init_pin_.GPIO_Pin   = Pin_N;
    init_pin_.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    init_pin_.GPIO_Speed = GPIO_Speed_50MHz;
    init_pin_.GPIO_OType = GPIO_OType_PP;
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIO_N, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM_N, ENABLE);
    GPIO_Init(GPIO_N, &init_pin_);
    GPIO_PinAFConfig(GPIO_N, GPIO_PinSource_N, GPIO_AF_TIM_N);
    // 选择内部时钟
    TIM_InternalClockConfig(TIM_N);

    TIM_TimeBaseInitTypeDef tim_init_struct;
    tim_init_struct.TIM_ClockDivision = TIM_CKD_DIV1;
    tim_init_struct.TIM_CounterMode   = TIM_CounterMode_Up;
    tim_init_struct.TIM_Period        = arr - 1; // ARR
    tim_init_struct.TIM_Prescaler     = psc - 1; // PSC
    // 1kHz 84mHz / 840 / 100
    tim_init_struct.TIM_RepetitionCounter = cnt;
    TIM_TimeBaseInit(TIM_N, &tim_init_struct);

    // 输出比较通道配置
    TIM_OCInitTypeDef tim_oc_init;
    // 给结构体赋初始值
    TIM_OCStructInit(&tim_oc_init);
    tim_oc_init.TIM_OCMode      = TIM_OCMode_PWM1;
    tim_oc_init.TIM_OCPolarity  = TIM_OCPolarity_High;
    tim_oc_init.TIM_OutputState = TIM_OutputState_Enable;
    tim_oc_init.TIM_Pulse       = crr; // CCR
    
    TIM_ARRPreloadConfig(TIM_N, ENABLE);
    //通道选择
    if (OC_Select == 1) {
        TIM_OC1Init(TIM_N, &tim_oc_init);
    } else if (OC_Select == 2) {
        TIM_OC2Init(TIM_N, &tim_oc_init);
    } else if (OC_Select == 3) {
        TIM_OC3Init(TIM_N, &tim_oc_init);
    } else if (OC_Select == 4) {
        TIM_OC4Init(TIM_N, &tim_oc_init);
    }

    TIM_Cmd(TIM_N, ENABLE);
}
