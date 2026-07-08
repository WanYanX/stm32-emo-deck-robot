#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lucy_audio.h"
#include "lucy_board.h"
#include "lucy_sr.h"
#include "lucy_wifi.h"
#include "lucy_ws_voice.h"

#define LUCY_STACK_WORDS(bytes) (((bytes) + sizeof(StackType_t) - 1) / sizeof(StackType_t))

void app_main(void)
{
    lucy_led_init();
    ESP_ERROR_CHECK_WITHOUT_ABORT(lucy_wifi_init_sta());
    ESP_ERROR_CHECK(lucy_mic_i2s_init());
    ESP_ERROR_CHECK(lucy_speaker_i2s_init());
    ESP_ERROR_CHECK_WITHOUT_ABORT(lucy_ws_voice_init());
    ESP_ERROR_CHECK(lucy_sr_init());

    BaseType_t ret = xTaskCreatePinnedToCore(lucy_audio_feed_task,
                                             "audio_feed",
                                             LUCY_STACK_WORDS(6 * 1024),
                                             lucy_sr_get_afe_data(),
                                             5,
                                             NULL,
                                             0);
    ESP_ERROR_CHECK(ret == pdPASS ? ESP_OK : ESP_FAIL);
    ret = xTaskCreatePinnedToCore(lucy_audio_detect_task,
                                  "audio_detect",
                                  LUCY_STACK_WORDS(16 * 1024),
                                  lucy_sr_get_afe_data(),
                                  5,
                                  NULL,
                                  1);
    ESP_ERROR_CHECK(ret == pdPASS ? ESP_OK : ESP_FAIL);
}
