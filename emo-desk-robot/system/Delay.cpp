#include "Delay.hpp"

/**
  * @brief  微秒级延时
  * @param  xus 延时时长，范围：0~233015
  * @retval 无
  */
void Delay_us(uint32_t xus)
{
	if (xus > 99864) xus = 99864;
    SysTick->LOAD = 168 * xus;           // 修改为16，因为1微秒需要计16个数
    SysTick->VAL = 0x00;
    SysTick->CTRL = 0x00000005;         // HCLK时钟源，启动定时器
    while(!(SysTick->CTRL & 0x00010000));
    SysTick->CTRL = 0x00000004;         // 关闭定时器
}

/**
  * @brief  毫秒级延时
  * @param  xms 延时时长，范围：0~4294967295
  * @retval 无
  */
void Delay_ms(uint32_t xms)
{
	while(xms--)
	{
		Delay_us(1000);
	}
}
 
/**
  * @brief  秒级延时
  * @param  xs 延时时长，范围：0~4294967295
  * @retval 无
  */
void Delay_s(uint32_t xs)
{
	while(xs--)
	{
		Delay_ms(1000);
	}
} 
