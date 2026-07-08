#include "lucy_sr.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "esp_afe_config.h"
#include "esp_afe_sr_models.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "model_path.h"

#include "lucy_audio.h"
#include "lucy_board.h"
#include "lucy_config.h"
#include "lucy_ws_voice.h"

static const char *TAG = "lucy_sr";

static const esp_afe_sr_iface_t *s_afe_handle;
static esp_afe_sr_data_t *s_afe_data;
static esp_mn_iface_t *s_multinet;
static model_iface_data_t *s_mn_model_data;
static volatile bool s_task_flag = true;
static volatile bool s_lucy_wakeup = false;
static volatile bool s_lucy_upload_started = false;
static volatile bool s_lucy_tone_done_seen = false;
static volatile bool s_lucy_vad_speech_seen = false;
static int64_t s_lucy_wakeup_deadline_ms;
static int64_t s_lucy_upload_ready_ms;
static int64_t s_lucy_vad_silence_start_ms;
static int s_afe_feed_chunksize;
static int s_afe_feed_channel;

static int64_t lucy_now_ms(void)
{
    return (int64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void lucy_set_wakeup(bool enabled)
{
    const bool was_enabled = s_lucy_wakeup;
    s_lucy_wakeup = enabled;
    if (enabled) {
        s_lucy_wakeup_deadline_ms = lucy_now_ms() + LUCY_WAKE_ACTIVE_MS;
        s_lucy_upload_started = false;
        s_lucy_tone_done_seen = false;
        s_lucy_upload_ready_ms = 0;
        s_lucy_vad_speech_seen = false;
        s_lucy_vad_silence_start_ms = 0;
        if (s_afe_handle && s_afe_data) {
            s_afe_handle->reset_vad(s_afe_data);
        }
    } else {
        s_lucy_upload_started = false;
        s_lucy_tone_done_seen = false;
        s_lucy_upload_ready_ms = 0;
        s_lucy_vad_speech_seen = false;
        s_lucy_vad_silence_start_ms = 0;
    }
    lucy_led_set(enabled);
    if (enabled && !was_enabled) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(lucy_ws_voice_start_session("你好露西"));
    } else if (!enabled && was_enabled) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(lucy_ws_voice_stop_session());
    }
    ESP_LOGI(TAG, "lucy_wakeup=%s", enabled ? "true" : "false");
}

static void lucy_update_vad_wakeup_state(const afe_fetch_result_t *res)
{
    const int64_t now_ms = lucy_now_ms();

    if (res->vad_state == VAD_SPEECH) {
        if (!s_lucy_vad_speech_seen) {
            ESP_LOGI(TAG, "VAD_START");
        }
        s_lucy_vad_speech_seen = true;
        s_lucy_vad_silence_start_ms = 0;
        s_lucy_wakeup_deadline_ms = now_ms + LUCY_WAKE_ACTIVE_MS;
        return;
    }

    if (res->vad_state == VAD_SILENCE && s_lucy_vad_speech_seen) {
        if (s_lucy_vad_silence_start_ms == 0) {
            s_lucy_vad_silence_start_ms = now_ms;
        }

        if (now_ms - s_lucy_vad_silence_start_ms >= LUCY_VAD_END_SILENCE_MS) {
            ESP_LOGI(TAG, "VAD_END: silence >= %d ms", LUCY_VAD_END_SILENCE_MS);
            lucy_set_wakeup(false);
        }
    }
}

static void lucy_send_pcm_placeholder(const int16_t *pcm, size_t samples)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(lucy_ws_voice_send_pcm(pcm, samples));
}

static bool lucy_voice_upload_ready(void)
{
    const int64_t now_ms = lucy_now_ms();

    if (s_lucy_upload_started) {
        return true;
    }

    if (lucy_wakeup_tone_is_playing()) {
        s_lucy_tone_done_seen = false;
        s_lucy_upload_ready_ms = 0;
        return false;
    }

    if (!s_lucy_tone_done_seen) {
        s_lucy_tone_done_seen = true;
        s_lucy_upload_ready_ms = now_ms + LUCY_WAKEUP_TONE_SETTLE_MS;
        if (s_afe_handle && s_afe_data) {
            s_afe_handle->reset_vad(s_afe_data);
        }
        ESP_LOGI(TAG, "wakeup tone done, wait %d ms before voice upload", LUCY_WAKEUP_TONE_SETTLE_MS);
        return false;
    }

    if (now_ms < s_lucy_upload_ready_ms) {
        return false;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(lucy_ws_voice_begin_upload());
    s_lucy_upload_started = true;
    return true;
}

esp_afe_sr_data_t *lucy_sr_get_afe_data(void)
{
    return s_afe_data;
}

void lucy_audio_feed_task(void *arg)
{
    esp_afe_sr_data_t *afe_data = (esp_afe_sr_data_t *)arg;
    const size_t raw_samples = s_afe_feed_chunksize;
    int32_t *raw_buffer = heap_caps_malloc(raw_samples * sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    int16_t *afe_buffer = heap_caps_malloc(s_afe_feed_chunksize * s_afe_feed_channel * sizeof(int16_t),
                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    if (!raw_buffer || !afe_buffer) {
        ESP_LOGE(TAG, "no memory for audio feed buffers");
        vTaskDelete(NULL);
    }

    while (s_task_flag) {
        size_t samples_read = 0;
        esp_err_t err = lucy_mic_read(raw_buffer, raw_samples, &samples_read);
        if (err != ESP_OK || samples_read == 0) {
            ESP_LOGW(TAG, "I2S mic read failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        for (int i = 0; i < s_afe_feed_chunksize; ++i) {
            int16_t sample = 0;
            if ((size_t)i < samples_read) {
                /*
                 * 32-bit I2S microphone samples are 24-bit PCM left-aligned in
                 * the slot. Convert to signed 16-bit PCM with >> 16. The old
                 * >> 14 path applied about +12 dB gain before AFE / Opus,
                 * clipping speech peaks and making ASR audio harsh / distorted.
                 */
                sample = (int16_t)(raw_buffer[i] >> 16);
            }

            for (int ch = 0; ch < s_afe_feed_channel; ++ch) {
                afe_buffer[i * s_afe_feed_channel + ch] = (ch == 0) ? sample : 0;
            }
        }

        s_afe_handle->feed(afe_data, afe_buffer);
    }

    free(raw_buffer);
    free(afe_buffer);
    vTaskDelete(NULL);
}

void lucy_audio_detect_task(void *arg)
{
    esp_afe_sr_data_t *afe_data = (esp_afe_sr_data_t *)arg;
    const int afe_chunksize = s_afe_handle->get_fetch_chunksize(afe_data);
    const int mn_chunksize = s_multinet->get_samp_chunksize(s_mn_model_data);
    assert(mn_chunksize == afe_chunksize);

    ESP_LOGI(TAG, "------------ MultiNet detect start ------------");
    ESP_LOGI(TAG, "say command: ni hao lu xi");

    while (s_task_flag) {
        afe_fetch_result_t *res = s_afe_handle->fetch(afe_data);
        if (!res || res->ret_value == ESP_FAIL) {
            ESP_LOGE(TAG, "AFE fetch error");
            break;
        }

        if (s_lucy_wakeup) {
            if (!lucy_voice_upload_ready()) {
                continue;
            }

            /*
             * Do not upload AFE vad_cache here. In this no-WakeNet flow upload
             * starts only after the local wakeup tone has finished and AFE/VAD
             * has already been reset, so the cache is not needed as wake-word
             * pre-roll. Some ESP-SR AFE versions may include current / overlapping
             * speech in vad_cache across fetches, which makes the server hear the
             * utterance prefix twice (for example "今天今天..."). Send only the
             * linear fetch output to keep the WebSocket stream monotonic.
             */
            lucy_send_pcm_placeholder(res->data, res->data_size / sizeof(int16_t));
            lucy_update_vad_wakeup_state(res);
            if (lucy_now_ms() >= s_lucy_wakeup_deadline_ms) {
                ESP_LOGI(TAG, "WAKEUP_END: max active timeout");
                lucy_set_wakeup(false);
            }
            /*
             * 唤醒后的提示音会被麦克风采集回来。如果继续把这段音频送给 MultiNet，
             * 很容易再次误识别成“你好露西”，导致提示音循环播放。
             * 唤醒窗口内先只转发/处理 PCM，不再做唤醒词检测。
             */
            continue;
        }

        /* 不使用 WakeNet：每次 AFE fetch 到音频后直接送给 MultiNet。 */
        esp_mn_state_t mn_state = s_multinet->detect(s_mn_model_data, res->data);

        if (mn_state == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t *mn_result = s_multinet->get_results(s_mn_model_data);
            if (!mn_result || mn_result->num <= 0) {
                s_multinet->clean(s_mn_model_data);
                continue;
            }

            const int command_id = mn_result->command_id[0];
            const int phrase_id = mn_result->phrase_id[0];
            const float prob = mn_result->prob[0];
            ESP_LOGI(TAG, "detected command_id=%d phrase_id=%d prob=%f string=%s raw=%s",
                     command_id, phrase_id, prob, mn_result->string, mn_result->raw_string);

            if (command_id == CMD_WAKE_LUCY) {
                ESP_LOGI(TAG, "识别到：你好露西");
                lucy_set_wakeup(true);
                ESP_ERROR_CHECK_WITHOUT_ABORT(lucy_play_wakeup_tone());
                /* 这里写“唤醒后”的机器人逻辑：联网、表情、进入对话模式等。 */
                s_multinet->clean(s_mn_model_data);
            }
        }

        if (mn_state == ESP_MN_STATE_TIMEOUT) {
            s_multinet->clean(s_mn_model_data);
        }
    }

    vTaskDelete(NULL);
}

esp_err_t lucy_sr_init(void)
{
    srmodel_list_t *models = esp_srmodel_init("model");
    ESP_RETURN_ON_FALSE(models, ESP_FAIL, TAG, "model partition not found");

    afe_config_t *afe_config = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    ESP_RETURN_ON_FALSE(afe_config, ESP_ERR_NO_MEM, TAG, "afe_config_init failed");
    afe_config->aec_init = false;
    afe_config->se_init = false;
    afe_config->wakenet_init = false;
    afe_config->vad_init = true;
    afe_config->vad_min_noise_ms = 1000;
    afe_config->vad_min_speech_ms = LUCY_VAD_MIN_SPEECH_MS;
    afe_config->vad_delay_ms = 128;
    afe_config->vad_mode = VAD_MODE_1;
    afe_config->fixed_output_channel = true;
    afe_config->pcm_config.sample_rate = LUCY_SAMPLE_RATE_HZ;

    s_afe_handle = esp_afe_handle_from_config(afe_config);
    ESP_RETURN_ON_FALSE(s_afe_handle, ESP_FAIL, TAG, "esp_afe_handle_from_config failed");
    s_afe_data = s_afe_handle->create_from_config(afe_config);
    afe_config_free(afe_config);
    ESP_RETURN_ON_FALSE(s_afe_data, ESP_FAIL, TAG, "create AFE failed");

    s_afe_handle->print_pipeline(s_afe_data);
    s_afe_feed_chunksize = s_afe_handle->get_feed_chunksize(s_afe_data);
    s_afe_feed_channel = s_afe_handle->get_feed_channel_num(s_afe_data);
    ESP_LOGI(TAG, "AFE feed_chunksize=%d feed_channel=%d fetch_chunksize=%d",
             s_afe_feed_chunksize, s_afe_feed_channel, s_afe_handle->get_fetch_chunksize(s_afe_data));

    char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_CHINESE);
    ESP_RETURN_ON_FALSE(mn_name, ESP_FAIL, TAG, "Chinese MultiNet model not found");

    s_multinet = esp_mn_handle_from_name(mn_name);
    ESP_RETURN_ON_FALSE(s_multinet, ESP_FAIL, TAG, "esp_mn_handle_from_name failed");
    s_mn_model_data = s_multinet->create(mn_name, LUCY_WAKE_ACTIVE_MS);
    ESP_RETURN_ON_FALSE(s_mn_model_data, ESP_FAIL, TAG, "create MultiNet failed");

    ESP_ERROR_CHECK(esp_mn_commands_alloc(s_multinet, s_mn_model_data));
    ESP_ERROR_CHECK(esp_mn_commands_clear());
    ESP_ERROR_CHECK(esp_mn_commands_add(CMD_WAKE_LUCY, "ni hao lu xi"));
    esp_mn_error_t *mn_error = esp_mn_commands_update();
    ESP_RETURN_ON_FALSE(!mn_error || mn_error->num == 0, ESP_FAIL, TAG, "update MultiNet commands failed");
    esp_mn_commands_print();
    s_multinet->print_active_speech_commands(s_mn_model_data);

    const int mn_chunksize = s_multinet->get_samp_chunksize(s_mn_model_data);
    const int afe_chunksize = s_afe_handle->get_fetch_chunksize(s_afe_data);
    ESP_RETURN_ON_FALSE(mn_chunksize == afe_chunksize, ESP_FAIL, TAG,
                        "MultiNet chunksize(%d) != AFE fetch chunksize(%d)", mn_chunksize, afe_chunksize);

    return ESP_OK;
}
