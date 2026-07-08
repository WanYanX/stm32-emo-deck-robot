#ifndef _LED_H_
#define _LED_H_

#include "stm32f4xx.h"
#include "stm32f4xx_gpio.h"

#include "Active.hpp"

class LED
{
public:
    LED()  = default;
    ~LED() = default;
    /*
     * @brief 初始化
     * @param RCC_AHB1Periph_GPIOx LED连接的GPIO端口时钟 RCC_APB1PeriphClockCmd
     * @param RCC_APB1Periph_TIMx LED连接的定时器时钟 RCC_APB1PeriphClockCmd
     * @param GPIOx LED连接的GPIO端口
     * @param pin LED连接的GPIO引脚
     * @param active_level LED的激活电平
     * @param mode GPIO模式，默认为输出模式
     * @param otype GPIO输出类型，默认为推挽输出
     * @param speed GPIO速度，默认为中速
     * @param pupd GPIO上下拉，默认为无上下拉
     */
    void init(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin_x, ActiveLevel active_level,
              uint32_t RCC_AHB1Periph_GPIOx, uint32_t RCC_APB1Periph_TIMx,
              GPIOMode_TypeDef mode = GPIO_Mode_OUT, GPIOOType_TypeDef otype = GPIO_OType_PP,
              GPIOSpeed_TypeDef speed = GPIO_Medium_Speed, GPIOPuPd_TypeDef pupd = GPIO_PuPd_NOPULL);

    // 开灯
    void turn_on();

    // 关灯
    void turn_off();

    LED(const LED &)            = delete;
    LED &operator=(const LED &) = delete;

private:
    // GPIO初始化结构体
    GPIO_InitTypeDef init_pin_;
    // GPIO端口
    GPIO_TypeDef *gpio_x_;

    // LED的激活电平
    ActiveLevel active_level_;
};

#endif