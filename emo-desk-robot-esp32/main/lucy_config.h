#pragma once

#include "driver/gpio.h"

/*
 * I2S 麦克风接线：VDD->3V3, GND->GND, SCK->IO5, WS->IO4, SD->IO6, L/R->GND。
 * MAX98357A 接线：VIN->3V3, GND->GND, DIN->IO15, BCLK->IO16, LRC->IO17, SD->IO7, GAIN->IO18。
 */
#define MIC_I2S_BCLK_GPIO GPIO_NUM_5
#define MIC_I2S_WS_GPIO GPIO_NUM_4
#define MIC_I2S_DIN_GPIO GPIO_NUM_6

#define SPK_I2S_DOUT_GPIO GPIO_NUM_15
#define SPK_I2S_BCLK_GPIO GPIO_NUM_16
#define SPK_I2S_WS_GPIO GPIO_NUM_17
#define SPK_SD_GPIO GPIO_NUM_7
#define SPK_GAIN_GPIO GPIO_NUM_18

/* MAX98357A GAIN 脚：false=低增益，true=高增益。 */
#ifndef LUCY_SPK_GAIN_HIGH
#define LUCY_SPK_GAIN_HIGH 0
#endif

/* 软件音量百分比，播放 PCM 前按比例缩放，避免 MAX98357A 输出过大。 */
#ifndef LUCY_SPK_VOLUME_PERCENT
#define LUCY_SPK_VOLUME_PERCENT 25
#endif

/* 板载 RGB/LED。暂时无法使用 */
#define LUCY_LED_GPIO GPIO_NUM_48

#ifndef LUCY_SAMPLE_RATE_HZ
#define LUCY_SAMPLE_RATE_HZ 16000
#endif

#ifndef LUCY_WAKE_ACTIVE_MS
#define LUCY_WAKE_ACTIVE_MS 10000
#endif

#ifndef LUCY_VAD_END_SILENCE_MS
#define LUCY_VAD_END_SILENCE_MS 1500
#endif

#ifndef LUCY_VAD_MIN_SPEECH_MS
#define LUCY_VAD_MIN_SPEECH_MS 256
#endif

#ifndef LUCY_WAKEUP_TONE_SETTLE_MS
/*
 * tools/nihao_female.wav: 24 kHz mono, 19200 samples, duration 800 ms.
 * lucy_audio waits until the full 800 ms prompt and the I2S silence flush are
 * finished before clearing s_wakeup_tone_playing, so this value is only the
 * extra acoustic settle margin after "wakeup tone done".
 */
#define LUCY_WAKEUP_TONE_SETTLE_MS 50
#endif

#ifndef CMD_WAKE_LUCY
#define CMD_WAKE_LUCY 1
#endif

#ifndef LUCY_WIFI_SSID
#define LUCY_WIFI_SSID "lucy"
#endif

#ifndef LUCY_WIFI_PASSWORD
#define LUCY_WIFI_PASSWORD "lucy12345678"
#endif

#ifndef LUCY_WIFI_MAX_RETRY
#define LUCY_WIFI_MAX_RETRY 10
#endif

#ifndef LUCY_WIFI_CONNECT_TIMEOUT_MS
#define LUCY_WIFI_CONNECT_TIMEOUT_MS 15000
#endif

#ifndef LUCY_WS_URI
#define LUCY_WS_URI "ws://192.168.8.109:8289/emorobot/v1/"
#endif

#ifndef LUCY_WS_AUTH_TOKEN
#define LUCY_WS_AUTH_TOKEN ""
#endif

#ifndef LUCY_WS_OPUS_FRAME_DURATION_MS
#define LUCY_WS_OPUS_FRAME_DURATION_MS 60
#endif

#ifndef LUCY_WS_OPUS_BITRATE
#define LUCY_WS_OPUS_BITRATE 24000
#endif

#ifndef LUCY_WS_OPUS_COMPLEXITY
#define LUCY_WS_OPUS_COMPLEXITY 3
#endif

#ifndef LUCY_WS_RX_BUFFER_BYTES
#define LUCY_WS_RX_BUFFER_BYTES 4096
#endif
