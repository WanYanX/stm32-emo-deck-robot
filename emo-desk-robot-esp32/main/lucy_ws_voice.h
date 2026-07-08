#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t lucy_ws_voice_init(void);
esp_err_t lucy_ws_voice_start_session(const char *wakeup_text);
esp_err_t lucy_ws_voice_begin_upload(void);
esp_err_t lucy_ws_voice_send_pcm(const int16_t *pcm, size_t samples);
esp_err_t lucy_ws_voice_stop_session(void);

