#include "lucy_audio.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "lucy_config.h"

static const char *TAG = "lucy_audio";

#define LUCY_SPK_WRITE_TIMEOUT_MS 1000
#define LUCY_AUDIO_CODEC_DMA_DESC_NUM 6
#define LUCY_AUDIO_CODEC_DMA_FRAME_NUM 240
#define LUCY_STACK_WORDS(bytes) (((bytes) + sizeof(StackType_t) - 1) / sizeof(StackType_t))

static i2s_chan_handle_t s_mic_rx_chan;
static i2s_chan_handle_t s_spk_tx_chan;
static volatile bool s_wakeup_tone_playing;
static SemaphoreHandle_t s_speaker_lock;

extern const uint32_t lucy_wakeup_pcm_sample_rate;
extern const size_t lucy_wakeup_pcm_samples;
extern const int16_t lucy_wakeup_pcm[];

static int16_t lucy_apply_volume(int16_t sample)
{
    int32_t scaled = ((int32_t)sample * LUCY_SPK_VOLUME_PERCENT) / 100;
    if (scaled > INT16_MAX) {
        return INT16_MAX;
    }
    if (scaled < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)scaled;
}

static void lucy_speaker_sd_set(bool enabled)
{
    gpio_set_level(SPK_SD_GPIO, enabled ? 1 : 0);
}

void lucy_speaker_gain_set(bool high_gain)
{
    gpio_set_level(SPK_GAIN_GPIO, high_gain ? 1 : 0);
}

esp_err_t lucy_mic_i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &s_mic_rx_chan), TAG, "create mic rx channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(LUCY_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_I2S_BCLK_GPIO,
            .ws = MIC_I2S_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = MIC_I2S_DIN_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_mic_rx_chan, &std_cfg), TAG, "init mic std mode failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_mic_rx_chan), TAG, "enable mic rx channel failed");
    return ESP_OK;
}

esp_err_t lucy_speaker_i2s_init(void)
{
    s_speaker_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_speaker_lock, ESP_ERR_NO_MEM, TAG, "create speaker mutex failed");

    gpio_config_t sd_cfg = {
        .pin_bit_mask = BIT64(SPK_SD_GPIO) | BIT64(SPK_GAIN_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&sd_cfg), TAG, "config speaker control gpio failed");
    lucy_speaker_sd_set(false);
    lucy_speaker_gain_set(LUCY_SPK_GAIN_HIGH);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = LUCY_AUDIO_CODEC_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = LUCY_AUDIO_CODEC_DMA_FRAME_NUM;
    chan_cfg.auto_clear_after_cb = true;
    chan_cfg.auto_clear_before_cb = false;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_spk_tx_chan, NULL), TAG, "create speaker tx channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(LUCY_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = SPK_I2S_BCLK_GPIO,
            .ws = SPK_I2S_WS_GPIO,
            .dout = SPK_I2S_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_spk_tx_chan, &std_cfg), TAG, "init speaker std mode failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_spk_tx_chan), TAG, "enable speaker tx channel failed");
    return ESP_OK;
}

esp_err_t lucy_mic_read(int32_t *buffer, size_t samples, size_t *samples_read)
{
    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(s_mic_rx_chan, buffer, samples * sizeof(int32_t), &bytes_read, portMAX_DELAY);
    if (samples_read) {
        *samples_read = bytes_read / sizeof(int32_t);
    }
    return err;
}

static esp_err_t lucy_speaker_write_raw_timeout(const void *buffer, size_t bytes, TickType_t timeout_ticks)
{
    const uint8_t *data = (const uint8_t *)buffer;
    size_t bytes_left = bytes;

    while (bytes_left > 0) {
        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(s_spk_tx_chan, data, bytes_left, &bytes_written, timeout_ticks);
        if (err != ESP_OK) {
            return err;
        }
        if (bytes_written == 0) {
            return ESP_ERR_TIMEOUT;
        }
        data += bytes_written;
        bytes_left -= bytes_written;
        taskYIELD();
    }

    return ESP_OK;
}

static esp_err_t lucy_speaker_write_raw(const void *buffer, size_t bytes)
{
    return lucy_speaker_write_raw_timeout(buffer, bytes, pdMS_TO_TICKS(LUCY_SPK_WRITE_TIMEOUT_MS));
}

esp_err_t lucy_speaker_write(const void *buffer, size_t bytes)
{
    ESP_RETURN_ON_FALSE(s_speaker_lock, ESP_ERR_INVALID_STATE, TAG, "speaker not initialized");
    if (xSemaphoreTake(s_speaker_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    lucy_speaker_sd_set(true);
    esp_err_t err = lucy_speaker_write_raw(buffer, bytes);
    lucy_speaker_sd_set(false);
    xSemaphoreGive(s_speaker_lock);
    return err;
}

esp_err_t lucy_speaker_stream_begin(void)
{
    ESP_RETURN_ON_FALSE(s_speaker_lock, ESP_ERR_INVALID_STATE, TAG, "speaker not initialized");
    if (xSemaphoreTake(s_speaker_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    lucy_speaker_sd_set(true);
    return ESP_OK;
}

esp_err_t lucy_speaker_stream_write(const void *buffer, size_t bytes)
{
    return lucy_speaker_write_raw(buffer, bytes);
}

void lucy_speaker_stream_end(void)
{
    int16_t silence[256] = {0};
    ESP_ERROR_CHECK_WITHOUT_ABORT(lucy_speaker_write_raw(silence, sizeof(silence)));
    lucy_speaker_sd_set(false);
    if (s_speaker_lock) {
        xSemaphoreGive(s_speaker_lock);
    }
}

static void lucy_wakeup_tone_task(void *arg)
{
    (void)arg;

    if (lucy_wakeup_pcm_sample_rate != LUCY_SAMPLE_RATE_HZ) {
        ESP_LOGW(TAG, "wakeup tone sample_rate=%" PRIu32 ", expected=%d",
                 lucy_wakeup_pcm_sample_rate, LUCY_SAMPLE_RATE_HZ);
    }

    size_t samples_left = lucy_wakeup_pcm_samples;
    size_t sample_offset = 0;
    const size_t chunk_samples = 256;
    int16_t play_buffer[256];

    ESP_LOGI(TAG, "play wakeup tone: %u Hz, %u samples, %u bytes",
             (unsigned)lucy_wakeup_pcm_sample_rate,
             (unsigned)lucy_wakeup_pcm_samples,
             (unsigned)(lucy_wakeup_pcm_samples * sizeof(int16_t)));
    ESP_LOGI(TAG, "speaker volume=%d%% gain=%s",
             LUCY_SPK_VOLUME_PERCENT,
             LUCY_SPK_GAIN_HIGH ? "high" : "low");

    if (!s_speaker_lock || xSemaphoreTake(s_speaker_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "skip wakeup tone: speaker busy");
        s_wakeup_tone_playing = false;
        vTaskDelete(NULL);
    }

    lucy_speaker_sd_set(true);

    while (samples_left > 0) {
        size_t write_samples = samples_left > chunk_samples ? chunk_samples : samples_left;
        for (size_t i = 0; i < write_samples; ++i) {
            play_buffer[i] = lucy_apply_volume(lucy_wakeup_pcm[sample_offset + i]);
        }

        esp_err_t err = lucy_speaker_write_raw(play_buffer, write_samples * sizeof(int16_t));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "wakeup tone i2s write failed: %s", esp_err_to_name(err));
            break;
        }
        sample_offset += write_samples;
        samples_left -= write_samples;
    }

    int16_t silence[256] = {0};
    ESP_ERROR_CHECK_WITHOUT_ABORT(lucy_speaker_write_raw(silence, sizeof(silence)));
    lucy_speaker_sd_set(false);
    xSemaphoreGive(s_speaker_lock);
    ESP_LOGI(TAG, "wakeup tone done");

    s_wakeup_tone_playing = false;
    vTaskDelete(NULL);
}

esp_err_t lucy_play_wakeup_tone(void)
{
    if (s_wakeup_tone_playing) {
        return ESP_OK;
    }

    s_wakeup_tone_playing = true;
    BaseType_t ret = xTaskCreatePinnedToCore(lucy_wakeup_tone_task,
                                             "wakeup_tone",
                                             LUCY_STACK_WORDS(3 * 1024),
                                             NULL,
                                             7,
                                             NULL,
                                             1);
    if (ret != pdPASS) {
        s_wakeup_tone_playing = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool lucy_wakeup_tone_is_playing(void)
{
    return s_wakeup_tone_playing;
}
