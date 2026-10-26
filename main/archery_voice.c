// main/archery_voice.c —— 实验：PTT 报靶语音上传与结果解析。
#include "archery_voice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp_audio.h"
#include "archery_score.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "nvs.h"

static const char *TAG = "archery_voice";

#define VOICE_NVS_NAMESPACE "archery_net"
#define VOICE_NVS_KEY "voice_url"
#define VOICE_URL_MAX 128
#define VOICE_CHUNK_BYTES 640  // 20 ms @ 16 kHz / 16 bit
#define VOICE_MAX_MS 8000
#define VOICE_RESPONSE_MAX 1200

typedef struct {
    char host[64];
    uint16_t port;
    char path[64];
} voice_endpoint_t;

static char s_url[VOICE_URL_MAX];
static char s_status[40] = "长按上键说话";
static char s_target_name[8] = "half";
static uint8_t s_arrows = 6;
static voice_endpoint_t s_endpoint;
static SemaphoreHandle_t s_start_sem;
static TaskHandle_t s_task;
static volatile bool s_active;
static volatile bool s_stop_requested;
static volatile bool s_result_ready;
static archery_voice_result_t s_result;

static bool parse_url(const char *url, voice_endpoint_t *out) {
    if (!url || !out) return false;
    if (strncmp(url, "http://", 7) != 0) return false;
    const char *rest = url + 7;
    const char *slash = strchr(rest, '/');
    if (!slash) return false;
    const char *colon = memchr(rest, ':', (size_t)(slash - rest));
    size_t host_len = colon ? (size_t)(colon - rest) : (size_t)(slash - rest);
    if (host_len == 0 || host_len >= sizeof(out->host)) return false;
    memcpy(out->host, rest, host_len);
    out->host[host_len] = '\0';
    out->port = 80;
    if (colon) {
        const long port = strtol(colon + 1, NULL, 10);
        if (port <= 0 || port > 65535) return false;
        out->port = (uint16_t)port;
    }
    if (strlen(slash) >= sizeof(out->path)) return false;
    strcpy(out->path, slash);
    return true;
}

static esp_err_t load_url(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(VOICE_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    size_t length = sizeof(s_url);
    s_url[0] = '\0';
    err = nvs_get_str(handle, VOICE_NVS_KEY, s_url, &length);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_url[0] = '\0';
        return ESP_OK;
    }
    return err;
}

esp_err_t archery_voice_save_url(const char *url) {
    if (!url) return ESP_ERR_INVALID_ARG;
    voice_endpoint_t probe;
    if (url[0] && !parse_url(url, &probe)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(VOICE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, VOICE_NVS_KEY, url);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) {
        strncpy(s_url, url, sizeof(s_url) - 1);
        s_url[sizeof(s_url) - 1] = '\0';
        archery_voice_set_status(s_url[0] ? "长按上键说话" : "语音未配置");
    }
    return err;
}

bool archery_voice_enabled(void) { return s_url[0] != '\0'; }
const char *archery_voice_url(void) { return s_url; }
bool archery_voice_active(void) { return s_active; }
const char *archery_voice_status(void) { return s_status; }

void archery_voice_set_status(const char *text) {
    if (!text) {
        s_status[0] = '\0';
        return;
    }
    strncpy(s_status, text, sizeof(s_status) - 1);
    s_status[sizeof(s_status) - 1] = '\0';
}

void archery_voice_set_context(uint8_t target, uint8_t arrows) {
    s_arrows = arrows ? arrows : 6;
    switch (target) {
    case ARCHERY_TARGET_FULL_40:
    case ARCHERY_TARGET_FULL_80:
    case ARCHERY_TARGET_FULL_122:
        strcpy(s_target_name, "full");
        break;
    default:
        strcpy(s_target_name, "half");
        break;
    }
}

bool archery_voice_take_result(archery_voice_result_t *out) {
    if (!s_result_ready || !out) return false;
    *out = s_result;
    s_result_ready = false;
    return true;
}

static int connect_to(const voice_endpoint_t *ep) {
    struct sockaddr_in dest = {0};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(ep->port);
    if (inet_pton(AF_INET, ep->host, &dest.sin_addr) != 1) {
        struct hostent *he = gethostbyname(ep->host);
        if (!he || !he->h_addr_list[0]) return -1;
        memcpy(&dest.sin_addr, he->h_addr_list[0], sizeof(dest.sin_addr));
    }
    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) != 0) {
        close(sock);
        return -1;
    }
    return sock;
}

static bool send_all(int sock, const void *data, size_t length) {
    const uint8_t *ptr = (const uint8_t *)data;
    while (length > 0) {
        const int sent = send(sock, ptr, length, 0);
        if (sent <= 0) return false;
        ptr += sent;
        length -= (size_t)sent;
    }
    return true;
}

static bool send_chunk(int sock, const void *data, size_t length) {
    char header[12];
    const int header_len = snprintf(header, sizeof(header), "%x\r\n", (unsigned)length);
    return send_all(sock, header, (size_t)header_len) &&
           send_all(sock, data, length) &&
           send_all(sock, "\r\n", 2);
}

static size_t read_response(int sock, char *buffer, size_t capacity) {
    size_t used = 0;
    while (used + 1 < capacity) {
        const int got = recv(sock, buffer + used, capacity - 1 - used, 0);
        if (got <= 0) break;
        used += (size_t)got;
        buffer[used] = '\0';
        if (strstr(buffer, "\r\n\r\n") && strstr(buffer, "}")) break;
    }
    buffer[used] = '\0';
    return used;
}

static const char *skip_spaces(const char *text) {
    while (*text == ' ' || *text == '\t') text++;
    return text;
}

static void parse_result(const char *json) {
    memset(&s_result, 0, sizeof(s_result));
    const char *ok = json ? strstr(json, "\"ok\"") : NULL;
    const char *ok_colon = ok ? strchr(ok, ':') : NULL;
    if (!ok_colon || strncmp(skip_spaces(ok_colon + 1), "true", 4) != 0) {
        strncpy(s_result.error, "parse", sizeof(s_result.error) - 1);
        return;
    }
    s_result.ok = true;
    const char *cursor = json;
    while (s_result.count < ARCHERY_VOICE_MAX_ITEMS) {
        const char *ring = strstr(cursor, "\"ring\"");
        if (!ring) break;
        const char *ring_colon = strchr(ring, ':');
        if (!ring_colon) break;
        int ring_value = -1;
        if (sscanf(skip_spaces(ring_colon + 1), "%d", &ring_value) != 1) break;
        const char *count = strstr(ring, "\"count\"");
        const char *next = strstr(ring + 6, "\"ring\"");
        if (!count || (next && count > next)) break;
        const char *count_colon = strchr(count, ':');
        if (!count_colon) break;
        const char *count_value = skip_spaces(count_colon + 1);
        archery_voice_item_t *item = &s_result.items[s_result.count];
        item->ring = (uint8_t)ring_value;
        if (strncmp(count_value, "null", 4) == 0) {
            item->all = true;
        } else {
            int parsed_count = -1;
            if (sscanf(count_value, "%d", &parsed_count) != 1) break;
            item->count = (uint8_t)parsed_count;
        }
        s_result.count++;
        cursor = next ? next : count_colon + 1;
    }
    if (s_result.count == 0) s_result.ok = false;
}

static void play_tone(uint16_t hz, uint16_t ms, int repeat, uint16_t gap_ms) {
    int16_t buffer[320];
    const int samples = (int)(16000 / hz / 2);
    for (int n = 0; n < repeat; n++) {
        int phase = 0;
        int remaining = (int)(16000L * ms / 1000);
        while (remaining > 0) {
            const int chunk = remaining < (int)(sizeof(buffer) / sizeof(buffer[0]))
                                  ? remaining : (int)(sizeof(buffer) / sizeof(buffer[0]));
            for (int i = 0; i < chunk; i++) {
                buffer[i] = phase < samples ? 5000 : -5000;
                if (++phase >= samples * 2) phase = 0;
            }
            (void)bsp_audio_write(buffer, (size_t)chunk * sizeof(buffer[0]));
            remaining -= chunk;
        }
        if (n + 1 < repeat && gap_ms) vTaskDelay(pdMS_TO_TICKS(gap_ms));
    }
}

static void run_session(void) {
    s_active = true;
    s_stop_requested = false;
    s_result_ready = false;
    archery_voice_set_status("录音中，松手结束");

    if (bsp_audio_init() != ESP_OK || bsp_audio_set_format(16000, 16, 1) != ESP_OK) {
        archery_voice_set_status("麦克风初始化失败");
        play_tone(600, 150, 2, 100);
        s_active = false;
        return;
    }

    const int sock = connect_to(&s_endpoint);
    if (sock < 0) {
        archery_voice_set_status("连不上语音服务");
        play_tone(600, 150, 2, 100);
        s_active = false;
        return;
    }

    char request[256];
    const int request_len = snprintf(
        request, sizeof(request),
        "POST %s HTTP/1.1\r\nHost: %s:%u\r\n"
        "Content-Type: audio/pcm;rate=16000;bits=16;channels=1\r\n"
        "X-Archery-Target: %s\r\nX-Archery-Arrows: %u\r\n"
        "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n",
        s_endpoint.path, s_endpoint.host, (unsigned)s_endpoint.port,
        s_target_name, (unsigned)s_arrows);
    if (!send_all(sock, request, (size_t)request_len)) {
        close(sock);
        archery_voice_set_status("发送失败");
        play_tone(600, 150, 2, 100);
        s_active = false;
        return;
    }

    uint8_t pcm[VOICE_CHUNK_BYTES];
    const int64_t started = esp_timer_get_time();
    bool sent_ok = true;
    while (!s_stop_requested && (esp_timer_get_time() - started) / 1000 < VOICE_MAX_MS) {
        if (bsp_audio_read(pcm, sizeof(pcm)) != ESP_OK) {
            sent_ok = false;
            break;
        }
        if (!send_chunk(sock, pcm, sizeof(pcm))) {
            sent_ok = false;
            break;
        }
    }
    send_all(sock, "0\r\n\r\n", 5);

    char response[VOICE_RESPONSE_MAX];
    const size_t got = read_response(sock, response, sizeof(response));
    close(sock);

    const char *body = strstr(response, "\r\n\r\n");
    if (sent_ok && body) {
        parse_result(body + 4);
    }
    if (!s_result.ok) {
        if (!s_result.error[0]) {
            strncpy(s_result.error, "network", sizeof(s_result.error) - 1);
        }
        archery_voice_set_status("没听清，请重说");
        play_tone(600, 150, 2, 100);
    } else {
        play_tone(1000, 80, 1, 0);
    }
    s_result_ready = true;
    s_active = false;
    if (s_result.ok) archery_voice_set_status("识别完成");
    ESP_LOGI(TAG, "session done: ok=%d items=%u bytes=%u", (int)s_result.ok,
             (unsigned)s_result.count, (unsigned)got);
}

static void voice_task(void *arg) {
    (void)arg;
    for (;;) {
        if (xSemaphoreTake(s_start_sem, portMAX_DELAY) == pdTRUE) {
            run_session();
        }
    }
}

esp_err_t archery_voice_init(void) {
    esp_err_t err = load_url();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "语音地址读取失败: %s", esp_err_to_name(err));
    }
    s_start_sem = xSemaphoreCreateBinary();
    if (!s_start_sem) return ESP_ERR_NO_MEM;
    if (xTaskCreate(voice_task, "voice", 10240, NULL, 4, &s_task) != pdPASS) {
        vSemaphoreDelete(s_start_sem);
        s_start_sem = NULL;
        return ESP_ERR_NO_MEM;
    }
    archery_voice_set_status(archery_voice_enabled() ? "长按上键说话" : "语音未配置");
    ESP_LOGI(TAG, "voice ready, url=%s", s_url[0] ? s_url : "(none)");
    return ESP_OK;
}

esp_err_t archery_voice_start(void) {
    if (!archery_voice_enabled()) return ESP_ERR_INVALID_STATE;
    if (!parse_url(s_url, &s_endpoint)) return ESP_ERR_INVALID_ARG;
    if (s_active) return ESP_ERR_INVALID_STATE;
    xSemaphoreGive(s_start_sem);
    return ESP_OK;
}

void archery_voice_stop(void) {
    if (s_active) s_stop_requested = true;
}
