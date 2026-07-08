#include "ResourceManager.hpp"

const ResourceManager::ServoPin ResourceManager::SERVO_BODY = {
    GPIO_Pin_12, GPIO_PinSource12, GPIO_AF_TIM4,
    GPIOD, TIM4, 1
};
const ResourceManager::ServoPin ResourceManager::SERVO_HEAD = {
    GPIO_Pin_13, GPIO_PinSource13, GPIO_AF_TIM4,
    GPIOD, TIM4, 2
};

GPIO_TypeDef* const ResourceManager::I2C_GPIO      = GPIOB;
GPIO_TypeDef* const ResourceManager::LED_GPIO       = GPIOF;
GPIO_TypeDef* const ResourceManager::ESP_RST_GPIO   = GPIOF;
GPIO_TypeDef* const ResourceManager::ESP_IO0_GPIO   = GPIOC;

const char ResourceManager::SERVER_IP[] = "192.168.8.103";
const char ResourceManager::WIFI_NAME[] = "lucy";
const char ResourceManager::WIFI_PSD[]  = "lucy12345678";
