#include "lucy_board.h"

#include "driver/gpio.h"
#include "esp_err.h"

#include "lucy_config.h"

void lucy_led_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << LUCY_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_ERROR_CHECK(gpio_set_level(LUCY_LED_GPIO, 0));
}

void lucy_led_set(bool enabled)
{
    ESP_ERROR_CHECK(gpio_set_level(LUCY_LED_GPIO, enabled ? 1 : 0));
}
