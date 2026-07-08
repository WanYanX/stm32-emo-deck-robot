#ifndef RESOURCE_MANAGER_HPP
#define RESOURCE_MANAGER_HPP

#include "stm32f4xx.h"
#include <cstdint>

class ResourceManager
{
public:
    struct ServoPin {
        uint16_t        pin;
        uint8_t         pinSource;
        uint8_t         af;
        GPIO_TypeDef*   port;
        TIM_TypeDef*    tim;
        uint8_t         oc;
    };
    static const ServoPin SERVO_BODY;
    static const ServoPin SERVO_HEAD;

    static constexpr uint32_t I2C_RCC_GPIO    = RCC_AHB1Periph_GPIOB;
    static constexpr uint32_t I2C_RCC_PERIPH  = RCC_APB1Periph_I2C1;
    static GPIO_TypeDef* const I2C_GPIO;
    static constexpr uint16_t I2C_SCL_PIN     = GPIO_Pin_6;
    static constexpr uint16_t I2C_SDA_PIN     = GPIO_Pin_7;
    static constexpr uint8_t  I2C_SCL_SOURCE  = GPIO_PinSource6;
    static constexpr uint8_t  I2C_SDA_SOURCE  = GPIO_PinSource7;
    static constexpr uint8_t  I2C_AF          = GPIO_AF_I2C1;

    static GPIO_TypeDef* const LED_GPIO;
    static constexpr uint16_t LED_PIN         = GPIO_Pin_9;
    static constexpr uint32_t LED_RCC_GPIO    = RCC_AHB1Periph_GPIOF;
    static constexpr uint32_t LED_RCC_TIM     = RCC_APB1Periph_TIM14;

    static GPIO_TypeDef* const ESP_RST_GPIO;
    static constexpr uint16_t      ESP_RST_PIN  = GPIO_Pin_6;
    static GPIO_TypeDef* const ESP_IO0_GPIO;
    static constexpr uint16_t      ESP_IO0_PIN  = GPIO_Pin_0;

    static constexpr uint32_t USART1_BAUD = 115200;
    static constexpr uint32_t USART3_BAUD = 115200;

    static constexpr uint8_t INIT_HEAD_ANGLE  = 120;
    static constexpr uint8_t INIT_BODY_ANGLE  = 95;

    static const char SERVER_IP[];
    static constexpr uint16_t    SERVER_PORT   = 8288;

    static const char WIFI_NAME[];
    static const char WIFI_PSD[];

    uint8_t head_angle() const { return head_angle_; }
    uint8_t body_angle() const { return body_angle_; }
    void set_head_angle(uint8_t v) { head_angle_ = v; }
    void set_body_angle(uint8_t v) { body_angle_ = v; }

public:
    static ResourceManager& instance()
    {
        static ResourceManager mgr;
        return mgr;
    }

private:
    ResourceManager() :
        head_angle_(INIT_HEAD_ANGLE),
        body_angle_(INIT_BODY_ANGLE) {}

    uint8_t head_angle_;
    uint8_t body_angle_;

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;
};

#endif
