#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t lucy_mic_i2s_init(void);
esp_err_t lucy_speaker_i2s_init(void);
esp_err_t lucy_mic_read(int32_t *buffer, size_t samples, size_t *samples_read);
void lucy_speaker_gain_set(bool high_gain);
esp_err_t lucy_speaker_write(const void *buffer, size_t bytes);
esp_err_t lucy_speaker_stream_begin(void);
esp_err_t lucy_speaker_stream_write(const void *buffer, size_t bytes);
void lucy_speaker_stream_end(void);
esp_err_t lucy_play_wakeup_tone(void);
bool lucy_wakeup_tone_is_playing(void);
