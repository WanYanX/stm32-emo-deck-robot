#include "lucy_ws_voice.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "opus.h"

#include "lucy_audio.h"
#include "lucy_config.h"
#include "lucy_wifi.h"

static const char *TAG = "lucy_ws_voice";

#define LUCY_WS_OP_TEXT 0x1
#define LUCY_WS_OP_BINARY 0x2
#define LUCY_OPUS_FRAME_SAMPLES ((LUCY_SAMPLE_RATE_HZ * LUCY_WS_OPUS_FRAME_DURATION_MS) / 1000)
#define LUCY_OPUS_MAX_PACKET_BYTES 4000
#define LUCY_PCM_STREAM_BYTES (LUCY_OPUS_FRAME_SAMPLES * sizeof(int16_t) * 8)
#define LUCY_OPUS_TX_MESSAGE_BYTES (LUCY_OPUS_MAX_PACKET_BYTES * 8)
#define LUCY_TTS_OPUS_MESSAGE_BYTES (LUCY_OPUS_MAX_PACKET_BYTES * 32)
#define LUCY_TTS_PCM_MESSAGE_BYTES (LUCY_OPUS_FRAME_SAMPLES * sizeof(int16_t) * 16)
#define LUCY_WS_TASK_STACK_BYTES (32 * 1024)
#define LUCY_TTS_IDLE_END_MS 500
#define LUCY_TTS_START_BUFFER_FRAMES 3
#define LUCY_TTS_START_BUFFER_WAIT_MS 120

/*
 * Follow xiaozhi-esp32 AudioService task model:
 * - Audio tasks are created by xTaskCreate/xTaskCreatePinnedToCore, so stacks
 *   are normal FreeRTOS stacks and stay in internal RAM under the sdkconfig
 *   internal-memory reservation policy.
 * - Large audio packet/PCM queues are separated from task stacks and placed in
 *   PSRAM explicitly in this C implementation.
 */
#define LUCY_OPUS_TX_TASK_STACK_BYTES (32 * 1024)
#define LUCY_WS_SEND_TASK_STACK_BYTES (8 * 1024)
#define LUCY_TTS_DECODE_TASK_STACK_BYTES (24 * 1024)
#define LUCY_TTS_PLAYBACK_TASK_STACK_BYTES (12 * 1024)
#define LUCY_AUDIO_QUEUE_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

static esp_websocket_client_handle_t s_ws_client;
static OpusEncoder *s_opus_encoder;
static OpusDecoder *s_opus_decoder;
static SemaphoreHandle_t s_opus_lock;
static StreamBufferHandle_t s_pcm_stream;
static MessageBufferHandle_t s_opus_tx_messages;
static MessageBufferHandle_t s_tts_opus_messages;
static MessageBufferHandle_t s_tts_pcm_messages;
static StaticStreamBuffer_t s_pcm_stream_static;
static StaticMessageBuffer_t s_opus_tx_messages_static;
static StaticMessageBuffer_t s_tts_opus_messages_static;
static StaticMessageBuffer_t s_tts_pcm_messages_static;
static uint8_t *s_pcm_stream_storage;
static uint8_t *s_opus_tx_storage;
static uint8_t *s_tts_opus_storage;
static uint8_t *s_tts_pcm_storage;
static bool s_connected;
static bool s_session_active;
static volatile bool s_tts_playing;
static volatile uint32_t s_tts_queued_frames;
static uint8_t s_opus_tx_packet[LUCY_OPUS_MAX_PACKET_BYTES];
static uint8_t s_ws_send_packet[LUCY_OPUS_MAX_PACKET_BYTES];
static uint8_t s_tts_opus_packet[LUCY_OPUS_MAX_PACKET_BYTES];
static uint8_t s_tts_ws_fragment_packet[LUCY_OPUS_MAX_PACKET_BYTES];
static int16_t s_opus_tx_pcm_frame[LUCY_OPUS_FRAME_SAMPLES];
static int16_t s_tts_pcm_frame[LUCY_OPUS_FRAME_SAMPLES];
static int16_t s_tts_playback_pcm_frame[LUCY_OPUS_FRAME_SAMPLES];
static char s_ws_headers[256];
static char s_session_id[32] = "esp32-1";
static size_t s_tts_ws_fragment_expected;
static size_t s_tts_ws_fragment_received;
static bool s_tts_ws_fragment_dropping;
static StaticTask_t s_opus_tx_tcb;
static StaticTask_t s_ws_send_tcb;
static StaticTask_t s_tts_decode_tcb;
static StaticTask_t s_tts_playback_tcb;
static StackType_t *s_opus_tx_stack;
static StackType_t *s_ws_send_stack;
static StackType_t *s_tts_decode_stack;
static StackType_t *s_tts_playback_stack;

static esp_err_t lucy_ws_encode_frame(const int16_t *pcm, size_t *encoded_bytes);
static void lucy_ws_drain_tts_pcm_queue(void);

static esp_err_t lucy_ws_create_internal_stack_task(TaskFunction_t task_func,
                                                    const char *task_name,
                                                    uint32_t stack_bytes,
                                                    UBaseType_t priority,
                                                    BaseType_t core_id,
                                                    StaticTask_t *tcb,
                                                    StackType_t **stack_out)
{
    /*
     * FreeRTOS stack depth is counted in StackType_t words, not bytes.
     * Passing byte counts here makes the TCB believe the stack is 4x larger
     * than the internal allocation on ESP32-S3, corrupting adjacent memory and
     * eventually resetting during cache-disabled panic/WDT paths.
     */
    const uint32_t stack_words = (stack_bytes + sizeof(StackType_t) - 1) / sizeof(StackType_t);
    const size_t alloc_bytes = stack_words * sizeof(StackType_t);
    StackType_t *stack = heap_caps_aligned_alloc(sizeof(StackType_t), alloc_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(stack, ESP_ERR_NO_MEM, TAG, "alloc internal stack for %s failed", task_name);

    TaskHandle_t handle = xTaskCreateStaticPinnedToCore(task_func,
                                                        task_name,
                                                        stack_words,
                                                        NULL,
                                                        priority,
                                                        stack,
                                                        tcb,
                                                        core_id);
    if (!handle) {
        heap_caps_free(stack);
        return ESP_ERR_NO_MEM;
    }

    *stack_out = stack;
    ESP_LOGI(TAG, "created %s with %u bytes internal stack (%u words)",
             task_name, (unsigned)alloc_bytes, (unsigned)stack_words);
    return ESP_OK;
}

static void lucy_ws_drain_pcm_stream(void)
{
    uint8_t discard[128];
    while (s_pcm_stream && xStreamBufferReceive(s_pcm_stream, discard, sizeof(discard), 0) > 0) {
    }
    while (s_opus_tx_messages && xMessageBufferReceive(s_opus_tx_messages, discard, sizeof(discard), 0) > 0) {
    }
}

static void lucy_ws_drain_tts_pcm_queue(void)
{
    uint8_t discard[256];
    while (s_tts_pcm_messages && xMessageBufferReceive(s_tts_pcm_messages, discard, sizeof(discard), 0) > 0) {
    }
}

static esp_err_t lucy_ws_send_text(const char *json)
{
    if (!s_ws_client || !s_connected || !json) {
        return ESP_ERR_INVALID_STATE;
    }

    const int len = (int)strlen(json);
    const int ret = esp_websocket_client_send_text(s_ws_client, json, len, pdMS_TO_TICKS(1000));
    if (ret < 0) {
        ESP_LOGW(TAG, "send text failed: %s", json);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "TX text: %s", json);
    return ESP_OK;
}

static esp_err_t lucy_ws_send_hello(void)
{
    char json[256];
    snprintf(json, sizeof(json),
             "{\"type\":\"hello\",\"version\":1,\"transport\":\"websocket\","
             "\"audio_params\":{\"format\":\"opus\",\"sample_rate\":%d,\"channels\":1,\"frame_duration\":%d}}",
             LUCY_SAMPLE_RATE_HZ, LUCY_WS_OPUS_FRAME_DURATION_MS);
    return lucy_ws_send_text(json);
}

static void lucy_ws_handle_text(const char *data, int len)
{
    if (!data || len <= 0) {
        return;
    }

    ESP_LOGI(TAG, "RX text: %.*s", len, data);

    const char *key = "\"session_id\"";
    const char *pos = strstr(data, key);
    if (!pos) {
        return;
    }
    pos = strchr(pos + strlen(key), ':');
    if (!pos) {
        return;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t') {
        pos++;
    }
    if (*pos != '\"') {
        return;
    }
    pos++;
    const char *end = strchr(pos, '\"');
    if (!end || end <= pos) {
        return;
    }

    const size_t copy_len = (size_t)(end - pos) < sizeof(s_session_id) - 1 ? (size_t)(end - pos) : sizeof(s_session_id) - 1;
    memcpy(s_session_id, pos, copy_len);
    s_session_id[copy_len] = '\0';
    ESP_LOGI(TAG, "session_id=%s", s_session_id);
}

static void lucy_ws_enqueue_tts_opus_frame(const uint8_t *data, size_t len)
{
    if (!data || len == 0 || !s_tts_opus_messages) {
        return;
    }

    if (len > LUCY_OPUS_MAX_PACKET_BYTES) {
        ESP_LOGW(TAG, "drop oversized tts opus frame: %u bytes", (unsigned)len);
        return;
    }

    const size_t sent = xMessageBufferSend(s_tts_opus_messages, data, len, 0);
    if (sent != len) {
        ESP_LOGW(TAG, "tts opus message buffer full, drop %u bytes", (unsigned)len);
        return;
    }
    s_tts_queued_frames++;
}

static void lucy_ws_handle_binary(const esp_websocket_event_data_t *event)
{
    if (!event || !event->data_ptr || event->data_len <= 0) {
        return;
    }

    const uint8_t *data = (const uint8_t *)event->data_ptr;
    const size_t len = (size_t)event->data_len;
    const size_t payload_len = event->payload_len > 0 ? (size_t)event->payload_len : len;
    const size_t payload_offset = event->payload_offset >= 0 ? (size_t)event->payload_offset : 0;

    if (payload_len <= len && payload_offset == 0) {
        lucy_ws_enqueue_tts_opus_frame(data, len);
        return;
    }

    if (payload_offset == 0) {
        s_tts_ws_fragment_expected = payload_len;
        s_tts_ws_fragment_received = 0;
        s_tts_ws_fragment_dropping = payload_len > LUCY_OPUS_MAX_PACKET_BYTES;
        if (s_tts_ws_fragment_dropping) {
            ESP_LOGW(TAG, "drop fragmented tts opus frame: %u bytes", (unsigned)payload_len);
        }
    }

    if (s_tts_ws_fragment_dropping) {
        return;
    }

    if (payload_len != s_tts_ws_fragment_expected || payload_offset != s_tts_ws_fragment_received ||
        payload_offset + len > sizeof(s_tts_ws_fragment_packet)) {
        ESP_LOGW(TAG, "drop broken tts fragment offset=%u len=%u total=%u",
                 (unsigned)payload_offset, (unsigned)len, (unsigned)payload_len);
        s_tts_ws_fragment_expected = 0;
        s_tts_ws_fragment_received = 0;
        s_tts_ws_fragment_dropping = true;
        return;
    }

    memcpy(s_tts_ws_fragment_packet + payload_offset, data, len);
    s_tts_ws_fragment_received += len;
    if (s_tts_ws_fragment_received == s_tts_ws_fragment_expected) {
        lucy_ws_enqueue_tts_opus_frame(s_tts_ws_fragment_packet, s_tts_ws_fragment_received);
        s_tts_ws_fragment_expected = 0;
        s_tts_ws_fragment_received = 0;
        s_tts_ws_fragment_dropping = false;
    }
}

static void lucy_ws_handle_text_in_event_task(const char *data, int len)
{
    /*
     * Keep this parser intentionally tiny: it runs in esp_websocket_client's
     * websocket_task. Heavy JSON handling must stay out of this callback.
     */
    lucy_ws_handle_text(data, len);
}

static void lucy_ws_tts_decode_task(void *arg)
{
    (void)arg;

    while (true) {
        const size_t len = xMessageBufferReceive(s_tts_opus_messages,
                                                 s_tts_opus_packet,
                                                 sizeof(s_tts_opus_packet),
                                                 pdMS_TO_TICKS(LUCY_TTS_IDLE_END_MS));
        if (len == 0) {
            continue;
        }
        if (s_tts_queued_frames > 0) {
            s_tts_queued_frames--;
        }

        if (!s_opus_decoder || !s_opus_lock) {
            continue;
        }

        if (xSemaphoreTake(s_opus_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGW(TAG, "opus lock timeout while decoding");
            continue;
        }
        const int samples = opus_decode(s_opus_decoder,
                                        s_tts_opus_packet,
                                        (opus_int32)len,
                                        s_tts_pcm_frame,
                                        LUCY_OPUS_FRAME_SAMPLES,
                                        0);
        xSemaphoreGive(s_opus_lock);

        if (samples < 0) {
            ESP_LOGW(TAG, "opus decode failed: %s", opus_strerror(samples));
            continue;
        }

        const size_t pcm_bytes = (size_t)samples * sizeof(int16_t);
        const size_t sent = xMessageBufferSend(s_tts_pcm_messages, s_tts_pcm_frame, pcm_bytes, pdMS_TO_TICKS(250));
        if (sent != pcm_bytes) {
            ESP_LOGW(TAG, "tts pcm playback queue full after wait, drop %u bytes", (unsigned)pcm_bytes);
        }
    }
}

static void lucy_ws_tts_playback_task(void *arg)
{
    (void)arg;
    bool playing = false;

    while (true) {
        const size_t pcm_bytes = xMessageBufferReceive(s_tts_pcm_messages,
                                                       s_tts_playback_pcm_frame,
                                                       sizeof(s_tts_playback_pcm_frame),
                                                       pdMS_TO_TICKS(LUCY_TTS_IDLE_END_MS));
        if (pcm_bytes == 0) {
            if (playing) {
                lucy_speaker_stream_end();
                playing = false;
                s_tts_playing = false;
                ESP_LOGI(TAG, "tts playback stream end");
            }
            continue;
        }

        if (!playing) {
            const int64_t prebuffer_deadline = (int64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) + LUCY_TTS_START_BUFFER_WAIT_MS;
            while (s_tts_queued_frames < LUCY_TTS_START_BUFFER_FRAMES &&
                   (int64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) < prebuffer_deadline) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            if (lucy_speaker_stream_begin() != ESP_OK) {
                ESP_LOGW(TAG, "speaker busy, drop tts stream");
                continue;
            }
            playing = true;
            s_tts_playing = true;
            ESP_LOGI(TAG, "tts playback stream begin");
        }
        esp_err_t write_err = lucy_speaker_stream_write(s_tts_playback_pcm_frame, pcm_bytes);
        if (write_err != ESP_OK) {
            ESP_LOGW(TAG, "tts i2s write failed: %s, stop current playback", esp_err_to_name(write_err));
            lucy_speaker_stream_end();
            playing = false;
            s_tts_playing = false;
            lucy_ws_drain_tts_pcm_queue();
            continue;
        }
    }
}

static void lucy_ws_opus_tx_task(void *arg)
{
    (void)arg;
    size_t filled_bytes = 0;

    while (true) {
        uint8_t *dst = (uint8_t *)s_opus_tx_pcm_frame + filled_bytes;
        const size_t need = sizeof(s_opus_tx_pcm_frame) - filled_bytes;
        const size_t got = xStreamBufferReceive(s_pcm_stream, dst, need, pdMS_TO_TICKS(100));
        if (got == 0) {
            continue;
        }
        filled_bytes += got;

        if (filled_bytes == sizeof(s_opus_tx_pcm_frame)) {
            if (s_session_active && s_connected) {
                size_t encoded_bytes = 0;
                esp_err_t err = lucy_ws_encode_frame(s_opus_tx_pcm_frame, &encoded_bytes);
                if (err == ESP_OK && encoded_bytes > 0) {
                    const size_t sent = xMessageBufferSend(s_opus_tx_messages, s_opus_tx_packet, encoded_bytes, 0);
                    if (sent != encoded_bytes) {
                        ESP_LOGW(TAG, "opus tx queue full, drop %u bytes", (unsigned)encoded_bytes);
                    }
                } else if (err != ESP_OK) {
                    ESP_LOGW(TAG, "opus encode frame failed: %s", esp_err_to_name(err));
                }
            }
            filled_bytes = 0;
        }
    }
}

static void lucy_ws_send_task(void *arg)
{
    (void)arg;

    while (true) {
        const size_t len = xMessageBufferReceive(s_opus_tx_messages,
                                                 s_ws_send_packet,
                                                 sizeof(s_ws_send_packet),
                                                 pdMS_TO_TICKS(100));
        if (len == 0) {
            continue;
        }
        if (!s_session_active || !s_connected) {
            continue;
        }

        const int ret = esp_websocket_client_send_bin(s_ws_client, (const char *)s_ws_send_packet, (int)len, pdMS_TO_TICKS(1000));
        if (ret < 0) {
            ESP_LOGW(TAG, "send opus frame failed, drop %u bytes", (unsigned)len);
        }
    }
}

static void lucy_ws_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "websocket connected");
        ESP_ERROR_CHECK_WITHOUT_ABORT(lucy_ws_send_hello());
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        s_connected = false;
        s_session_active = false;
        lucy_ws_drain_pcm_stream();
        ESP_LOGW(TAG, "websocket disconnected");
        break;
    case WEBSOCKET_EVENT_DATA:
        if (!data) {
            break;
        }
        if (data->op_code == LUCY_WS_OP_BINARY) {
            lucy_ws_handle_binary(data);
        } else if (data->op_code == LUCY_WS_OP_TEXT) {
            lucy_ws_handle_text_in_event_task(data->data_ptr, data->data_len);
        } else {
            ESP_LOGD(TAG, "ignore websocket opcode=%d len=%d", data->op_code, data->data_len);
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGW(TAG, "websocket error");
        break;
    default:
        break;
    }
}

static esp_err_t lucy_ws_init_opus(void)
{
    int err = OPUS_OK;
    s_opus_encoder = opus_encoder_create(LUCY_SAMPLE_RATE_HZ, 1, OPUS_APPLICATION_VOIP, &err);
    ESP_RETURN_ON_FALSE(s_opus_encoder && err == OPUS_OK, ESP_FAIL, TAG, "create opus encoder failed: %s", opus_strerror(err));
    opus_encoder_ctl(s_opus_encoder, OPUS_SET_BITRATE(LUCY_WS_OPUS_BITRATE));
    opus_encoder_ctl(s_opus_encoder, OPUS_SET_COMPLEXITY(LUCY_WS_OPUS_COMPLEXITY));
    opus_encoder_ctl(s_opus_encoder, OPUS_SET_VBR(0));

    s_opus_decoder = opus_decoder_create(LUCY_SAMPLE_RATE_HZ, 1, &err);
    ESP_RETURN_ON_FALSE(s_opus_decoder && err == OPUS_OK, ESP_FAIL, TAG, "create opus decoder failed: %s", opus_strerror(err));

    s_opus_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_opus_lock, ESP_ERR_NO_MEM, TAG, "create opus mutex failed");
    /*
     * xiaozhi keeps task stacks as normal FreeRTOS stacks and separates audio
     * data flow with queues.  This C version uses FreeRTOS static stream/message
     * buffers whose storage is explicitly allocated from PSRAM, keeping internal
     * RAM for stacks, Wi-Fi, I2S DMA and interrupt/runtime critical paths.
     */
    s_pcm_stream_storage = heap_caps_malloc(LUCY_PCM_STREAM_BYTES + 1, LUCY_AUDIO_QUEUE_CAPS);
    ESP_RETURN_ON_FALSE(s_pcm_stream_storage, ESP_ERR_NO_MEM, TAG, "create pcm stream psram storage failed");
    s_pcm_stream = xStreamBufferCreateStatic(LUCY_PCM_STREAM_BYTES,
                                             sizeof(int16_t),
                                             s_pcm_stream_storage,
                                             &s_pcm_stream_static);
    ESP_RETURN_ON_FALSE(s_pcm_stream, ESP_ERR_NO_MEM, TAG, "create pcm stream failed");

    s_opus_tx_storage = heap_caps_malloc(LUCY_OPUS_TX_MESSAGE_BYTES + 1, LUCY_AUDIO_QUEUE_CAPS);
    ESP_RETURN_ON_FALSE(s_opus_tx_storage, ESP_ERR_NO_MEM, TAG, "create opus tx psram storage failed");
    s_opus_tx_messages = xMessageBufferCreateStatic(LUCY_OPUS_TX_MESSAGE_BYTES,
                                                    s_opus_tx_storage,
                                                    &s_opus_tx_messages_static);
    ESP_RETURN_ON_FALSE(s_opus_tx_messages, ESP_ERR_NO_MEM, TAG, "create opus tx message buffer failed");

    s_tts_opus_storage = heap_caps_malloc(LUCY_TTS_OPUS_MESSAGE_BYTES + 1, LUCY_AUDIO_QUEUE_CAPS);
    ESP_RETURN_ON_FALSE(s_tts_opus_storage, ESP_ERR_NO_MEM, TAG, "create tts opus psram storage failed");
    s_tts_opus_messages = xMessageBufferCreateStatic(LUCY_TTS_OPUS_MESSAGE_BYTES,
                                                      s_tts_opus_storage,
                                                      &s_tts_opus_messages_static);
    ESP_RETURN_ON_FALSE(s_tts_opus_messages, ESP_ERR_NO_MEM, TAG, "create tts opus message buffer failed");

    s_tts_pcm_storage = heap_caps_malloc(LUCY_TTS_PCM_MESSAGE_BYTES + 1, LUCY_AUDIO_QUEUE_CAPS);
    ESP_RETURN_ON_FALSE(s_tts_pcm_storage, ESP_ERR_NO_MEM, TAG, "create tts pcm psram storage failed");
    s_tts_pcm_messages = xMessageBufferCreateStatic(LUCY_TTS_PCM_MESSAGE_BYTES,
                                                    s_tts_pcm_storage,
                                                    &s_tts_pcm_messages_static);
    ESP_RETURN_ON_FALSE(s_tts_pcm_messages, ESP_ERR_NO_MEM, TAG, "create tts pcm message buffer failed");
    return ESP_OK;
}

esp_err_t lucy_ws_voice_init(void)
{
    ESP_RETURN_ON_ERROR(lucy_ws_init_opus(), TAG, "opus init failed");

    uint8_t mac[6] = {0};
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(s_ws_headers, sizeof(s_ws_headers),
             "Protocol-Version: 1\r\n"
             "Device-Id: %02x%02x%02x%02x%02x%02x\r\n"
             "Client-Id: %02x%02x%02x%02x%02x%02x\r\n"
             "Authorization: Bearer %s\r\n",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             LUCY_WS_AUTH_TOKEN);

    esp_websocket_client_config_t ws_cfg = {
        .uri = LUCY_WS_URI,
        .headers = s_ws_headers,
        .buffer_size = LUCY_WS_RX_BUFFER_BYTES,
        .network_timeout_ms = 10000,
        .reconnect_timeout_ms = 5000,
        .task_stack = LUCY_WS_TASK_STACK_BYTES,
    };

    s_ws_client = esp_websocket_client_init(&ws_cfg);
    ESP_RETURN_ON_FALSE(s_ws_client, ESP_FAIL, TAG, "esp_websocket_client_init failed");
    ESP_RETURN_ON_ERROR(esp_websocket_register_events(s_ws_client, WEBSOCKET_EVENT_ANY, lucy_ws_event_handler, NULL),
                        TAG, "register websocket events failed");

    /*
     * xiaozhi relies on sdkconfig to keep normal FreeRTOS task stacks internal.
     * This C project has SPIRAM_USE_MALLOC enabled, so explicitly allocate these
     * audio task stacks from internal RAM to rule out PSRAM-stack access during
     * panic/cache-disabled paths. Large audio queues still live in PSRAM.
     */
    ESP_RETURN_ON_ERROR(lucy_ws_create_internal_stack_task(lucy_ws_opus_tx_task,
                                                           "ws_opus_tx",
                                                           LUCY_OPUS_TX_TASK_STACK_BYTES,
                                                           3,
                                                           0,
                                                           &s_opus_tx_tcb,
                                                           &s_opus_tx_stack),
                        TAG, "create opus tx task failed");

    ESP_RETURN_ON_ERROR(lucy_ws_create_internal_stack_task(lucy_ws_send_task,
                                                           "ws_send",
                                                           LUCY_WS_SEND_TASK_STACK_BYTES,
                                                           4,
                                                           0,
                                                           &s_ws_send_tcb,
                                                           &s_ws_send_stack),
                        TAG, "create ws send task failed");

    ESP_RETURN_ON_ERROR(lucy_ws_create_internal_stack_task(lucy_ws_tts_decode_task,
                                                           "ws_tts_dec",
                                                           LUCY_TTS_DECODE_TASK_STACK_BYTES,
                                                           3,
                                                           1,
                                                           &s_tts_decode_tcb,
                                                           &s_tts_decode_stack),
                        TAG, "create tts decode task failed");

    ESP_RETURN_ON_ERROR(lucy_ws_create_internal_stack_task(lucy_ws_tts_playback_task,
                                                           "ws_tts_play",
                                                           LUCY_TTS_PLAYBACK_TASK_STACK_BYTES,
                                                           4,
                                                           1,
                                                           &s_tts_playback_tcb,
                                                           &s_tts_playback_stack),
                        TAG, "create tts playback task failed");

    if (!lucy_wifi_is_connected()) {
        ESP_LOGW(TAG, "WiFi not connected yet, websocket client will still start and reconnect");
    }
    ESP_RETURN_ON_ERROR(esp_websocket_client_start(s_ws_client), TAG, "start websocket client failed");
    ESP_LOGI(TAG, "websocket voice client started: %s", LUCY_WS_URI);
    return ESP_OK;
}

esp_err_t lucy_ws_voice_start_session(const char *wakeup_text)
{
    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    char json[192];
    snprintf(json, sizeof(json),
             "{\"session_id\":\"%s\",\"type\":\"listen\",\"state\":\"detect\",\"text\":\"%s\"}",
             s_session_id, wakeup_text ? wakeup_text : "你好露西");
    ESP_RETURN_ON_ERROR(lucy_ws_send_text(json), TAG, "send listen/detect failed");

    s_session_active = false;
    lucy_ws_drain_pcm_stream();
    ESP_LOGI(TAG, "voice session detected, wait before upload");
    return ESP_OK;
}

esp_err_t lucy_ws_voice_begin_upload(void)
{
    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_session_active) {
        return ESP_OK;
    }

    char json[160];
    snprintf(json, sizeof(json),
             "{\"session_id\":\"%s\",\"type\":\"listen\",\"state\":\"start\",\"mode\":\"auto\"}",
             s_session_id);
    ESP_RETURN_ON_ERROR(lucy_ws_send_text(json), TAG, "send listen/start failed");

    s_session_active = true;
    lucy_ws_drain_pcm_stream();
    ESP_LOGI(TAG, "voice upload started");
    return ESP_OK;
}

static esp_err_t lucy_ws_encode_frame(const int16_t *pcm, size_t *encoded_bytes)
{
    if (encoded_bytes) {
        *encoded_bytes = 0;
    }
    if (xSemaphoreTake(s_opus_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    const int bytes = opus_encode(s_opus_encoder, pcm, LUCY_OPUS_FRAME_SAMPLES, s_opus_tx_packet, sizeof(s_opus_tx_packet));
    xSemaphoreGive(s_opus_lock);

    if (bytes < 0) {
        ESP_LOGW(TAG, "opus encode failed: %s", opus_strerror(bytes));
        return ESP_FAIL;
    }

    if (encoded_bytes) {
        *encoded_bytes = (size_t)bytes;
    }
    return ESP_OK;
}

esp_err_t lucy_ws_voice_send_pcm(const int16_t *pcm, size_t samples)
{
    if (!s_session_active || !s_connected || s_tts_playing || !pcm || samples == 0) {
        return ESP_OK;
    }

    const size_t bytes = samples * sizeof(int16_t);
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(80);
    while (xStreamBufferSpacesAvailable(s_pcm_stream) < bytes && xTaskGetTickCount() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (xStreamBufferSpacesAvailable(s_pcm_stream) < bytes) {
        ESP_LOGW(TAG, "pcm stream full after wait, drop %u bytes", (unsigned)bytes);
        return ESP_OK;
    }

    const size_t sent = xStreamBufferSend(s_pcm_stream, pcm, bytes, 0);
    if (sent < bytes) {
        ESP_LOGW(TAG, "pcm stream full after wait, drop %u bytes", (unsigned)(bytes - sent));
    }

    return ESP_OK;
}

esp_err_t lucy_ws_voice_stop_session(void)
{
    if (!s_session_active) {
        return ESP_OK;
    }

    s_session_active = false;
    lucy_ws_drain_pcm_stream();
    char json[128];
    snprintf(json, sizeof(json),
             "{\"session_id\":\"%s\",\"type\":\"listen\",\"state\":\"stop\"}",
             s_session_id);
    ESP_RETURN_ON_ERROR(lucy_ws_send_text(json), TAG, "send listen/stop failed");
    ESP_LOGI(TAG, "voice upload stopped");
    return ESP_OK;
}
