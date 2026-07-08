#pragma once

#include "esp_err.h"
#include "esp_afe_sr_iface.h"

esp_err_t lucy_sr_init(void);
esp_afe_sr_data_t *lucy_sr_get_afe_data(void);
void lucy_audio_feed_task(void *arg);
void lucy_audio_detect_task(void *arg);
