// main/archery_ws_client.c —— 豆包流式识别（WSS）客户端实现。
#include "archery_ws_client.h"

#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_tls.h"

static const char *TAG = "ws_client";

#define WS_HOST "openspeech.bytedance.com"
#define WS_PATH "/api/v3/sauc/bigmodel_async"
#define WS_RESOURCE "volc.seedasr.sauc.duration"
#define FRAME_BUF 4096
#define MAX_AUDIO_BYTES 16000  // 0.5 s @ 16 kHz/16 bit，单帧上限

static esp_tls_t *s_tls;
static uint8_t s_frame[FRAME_BUF];
static int s_last_status;
static int s_last_open_result = -1;

int archery_ws_last_open_result(void) { return s_last_open_result; }

int archery_ws_last_status(void) { return s_last_status; }

static bool write_all(const void *data, size_t length) {
    const uint8_t *ptr = (const uint8_t *)data;
    while (length > 0) {
        const ssize_t sent = esp_tls_conn_write(s_tls, ptr, length);
        if (sent <= 0) return false;
        ptr += sent;
        length -= (size_t)sent;
    }
    return true;
}

static bool read_exact(void *data, size_t length) {
    uint8_t *ptr = (uint8_t *)data;
    while (length > 0) {
        const ssize_t got = esp_tls_conn_read(s_tls, ptr, length);
        if (got <= 0) return false;
        ptr += got;
        length -= (size_t)got;
    }
    return true;
}

static int base64_encode(const uint8_t *src, size_t length, char *out, size_t cap) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t used = 0;
    for (size_t i = 0; i < length; i += 3) {
        const uint32_t a = src[i];
        const uint32_t b = i + 1 < length ? src[i + 1] : 0;
        const uint32_t c = i + 2 < length ? src[i + 2] : 0;
        const uint32_t v = (a << 16) | (b << 8) | c;
        if (used + 4 >= cap) return -1;
        out[used++] = table[(v >> 18) & 0x3F];
        out[used++] = table[(v >> 12) & 0x3F];
        out[used++] = i + 1 < length ? table[(v >> 6) & 0x3F] : '=';
        out[used++] = i + 2 < length ? table[v & 0x3F] : '=';
    }
    out[used] = '\0';
    return (int)used;
}

static bool ws_send_frame(uint8_t opcode, const uint8_t *payload, size_t length) {
    uint8_t header[14];
    size_t used = 0;
    header[used++] = (uint8_t)(0x80 | opcode);
    if (length < 126) {
        header[used++] = (uint8_t)(0x80 | length);
    } else if (length < 65536) {
        header[used++] = (uint8_t)(0x80 | 126);
        header[used++] = (uint8_t)(length >> 8);
        header[used++] = (uint8_t)(length & 0xFF);
    } else {
        header[used++] = (uint8_t)(0x80 | 127);
        for (int shift = 56; shift >= 0; shift -= 8) {
            header[used++] = (uint8_t)((uint64_t)length >> shift);
        }
    }
    uint8_t mask[4] = {(uint8_t)esp_random(), (uint8_t)esp_random(),
                       (uint8_t)esp_random(), (uint8_t)esp_random()};
    memcpy(header + used, mask, 4);
    used += 4;
    if (!write_all(header, used)) return false;
    uint8_t buffer[512];
    for (size_t offset = 0; offset < length;) {
        const size_t part = (length - offset) < sizeof(buffer) ? (length - offset)
                                                              : sizeof(buffer);
        for (size_t i = 0; i < part; i++) {
            buffer[i] = payload[offset + i] ^ mask[(offset + i) & 3u];
        }
        if (!write_all(buffer, part)) return false;
        offset += part;
    }
    return true;
}

static int ws_read_frame(uint8_t *out, size_t cap, uint8_t *opcode) {
    uint8_t head[2];
    if (!read_exact(head, sizeof(head))) return -1;
    *opcode = head[0] & 0x0F;
    const bool masked = (head[1] & 0x80) != 0;
    uint64_t length = head[1] & 0x7F;
    if (length == 126) {
        uint8_t ext[2];
        if (!read_exact(ext, sizeof(ext))) return -1;
        length = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (length == 127) {
        uint8_t ext[8];
        if (!read_exact(ext, sizeof(ext))) return -1;
        length = 0;
        for (int i = 0; i < 8; i++) length = (length << 8) | ext[i];
    }
    uint8_t mask[4] = {0};
    if (masked && !read_exact(mask, sizeof(mask))) return -1;
    if (length > cap) return -2;
    if (!read_exact(out, (size_t)length)) return -1;
    if (masked) {
        for (size_t i = 0; i < length; i++) out[i] ^= mask[i & 3u];
    }
    return (int)length;
}

static bool handle_frame(const uint8_t *payload, int length, char *text, size_t cap) {
    if (length < 8) return false;
    const uint8_t flags = payload[1] & 0x0F;
    int offset = 4;
    if (flags & 0x01) offset += 4;
    if (length < offset + 4) return false;
    const uint32_t size = ((uint32_t)payload[offset] << 24) |
                          ((uint32_t)payload[offset + 1] << 16) |
                          ((uint32_t)payload[offset + 2] << 8) | payload[offset + 3];
    offset += 4;
    uint32_t available = (uint32_t)(length - offset);
    if (size < available) available = size;
    text[0] = '\0';
    const char *begin = (const char *)payload + offset;
    const char *end = begin + available;
    const char *key = strstr(begin, "\"text\"");
    if (key && key < end) {
        const char *colon = strchr(key, ':');
        if (colon && colon < end) {
            const char *value = colon + 1;
            while (value < end && (*value == ' ' || *value == '\t')) value++;
            if (value < end && *value == '"') {
                value++;
                size_t used = 0;
                for (const char *p = value; p < end && *p; p++) {
                    if (*p == '\\' && p + 1 < end && p[1] == '"') {
                        if (used + 1 < cap) text[used++] = '"';
                        p++;
                        continue;
                    }
                    if (*p == '"') break;
                    if (used + 1 < cap) text[used++] = *p;
                }
                text[used] = '\0';
            }
        }
    }
    return (flags & 0x02) != 0;
}

static bool send_init(void) {
    static const char json[] =
        "{\"audio\":{\"format\":\"pcm\",\"codec\":\"raw\",\"sample_rate\":16000,"
        "\"channel\":1},\"request\":{\"model_name\":\"bigmodel\","
        "\"enable_punc\":false,\"enable_itn\":false}}";
    uint8_t packet[512];
    size_t used = 0;
    packet[used++] = 0x11;
    packet[used++] = 0x11;
    packet[used++] = 0x10;
    packet[used++] = 0x00;
    packet[used++] = 0x00;
    packet[used++] = 0x00;
    packet[used++] = 0x00;
    packet[used++] = 0x01;
    const size_t size = strlen(json);
    packet[used++] = (uint8_t)(size >> 24);
    packet[used++] = (uint8_t)(size >> 16);
    packet[used++] = (uint8_t)(size >> 8);
    packet[used++] = (uint8_t)size;
    memcpy(packet + used, json, size);
    used += size;
    return ws_send_frame(0x02, packet, used);
}

bool archery_ws_connected(void) { return s_tls != NULL; }

bool archery_ws_open(const char *appid, const char *token) {
    archery_ws_close();
    s_last_status = 0;
    esp_tls_cfg_t cfg = {
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };
    s_tls = esp_tls_init();
    if (!s_tls) return false;
    if (esp_tls_conn_new_sync(WS_HOST, strlen(WS_HOST), 443, &cfg, s_tls) != 1) {
        ESP_LOGE(TAG, "TLS connect failed");
        archery_ws_close();
        return false;
    }

    uint8_t nonce[16];
    for (int i = 0; i < 16; i++) nonce[i] = (uint8_t)esp_random();
    char key[32];
    if (base64_encode(nonce, sizeof(nonce), key, sizeof(key)) < 0) {
        archery_ws_close();
        return false;
    }
    char request[768];
    const int request_len = snprintf(
        request, sizeof(request),
        "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n"
        "X-Api-App-Key: %s\r\nX-Api-Access-Key: %s\r\n"
        "X-Api-Resource-Id: %s\r\nX-Api-Request-Id: req-%08x%08x\r\n\r\n",
        WS_PATH, WS_HOST, key, appid, token, WS_RESOURCE, (unsigned)esp_random(),
        (unsigned)esp_random());
    if (!write_all(request, (size_t)request_len)) {
        archery_ws_close();
        return false;
    }

    static char response[2048];
    size_t used = 0;
    while (used + 1 < sizeof(response)) {
        const ssize_t got = esp_tls_conn_read(s_tls, response + used, 1);
        if (got <= 0) break;
        used++;
        response[used] = '\0';
        if (strstr(response, "\r\n\r\n") || strstr(response, "\n\n")) break;
    }
    if (!strstr(response, "101")) {
        int code = 0;
        if (sscanf(response, "HTTP/1.%*d %d", &code) == 1) s_last_status = code;
        ESP_LOGE(TAG, "handshake failed: %.40s", response);
        archery_ws_close();
        return false;
    }
    if (!send_init()) {
        s_last_open_result = 0;
        archery_ws_close();
        return false;
    }
    s_last_open_result = 1;
    // 消费 init 回应，避免它被当成识别结果。
    uint8_t opcode = 0;
    (void)ws_read_frame(s_frame, sizeof(s_frame), &opcode);
    return true;
}

bool archery_ws_send_audio(const int16_t *pcm, size_t samples, bool last) {
    if (!s_tls) return false;
    const size_t bytes = samples * sizeof(int16_t);
    if (bytes > MAX_AUDIO_BYTES) return false;
    static uint8_t packet[MAX_AUDIO_BYTES + 8];
    size_t used = 0;
    packet[used++] = 0x11;
    packet[used++] = (uint8_t)(0x20 | (last ? 0x02 : 0x00));
    packet[used++] = 0x10;
    packet[used++] = 0x00;
    packet[used++] = (uint8_t)(bytes >> 24);
    packet[used++] = (uint8_t)(bytes >> 16);
    packet[used++] = (uint8_t)(bytes >> 8);
    packet[used++] = (uint8_t)bytes;
    memcpy(packet + used, pcm, bytes);
    used += bytes;
    return ws_send_frame(0x02, packet, used);
}

bool archery_ws_read(char *text, size_t capacity) {
    if (!s_tls) return false;
    for (int attempt = 0; attempt < 8; attempt++) {
        uint8_t opcode = 0;
        const int len = ws_read_frame(s_frame, sizeof(s_frame), &opcode);
        if (len <= 0) return false;
        if (opcode == 0x09) {
            (void)ws_send_frame(0x0A, s_frame, (size_t)len);
            continue;
        }
        if (opcode == 0x08) return false;
        if (opcode != 0x02 && opcode != 0x00) continue;
        return handle_frame(s_frame, len, text, capacity);
    }
    return false;
}

void archery_ws_close(void) {
    if (s_tls) {
        esp_tls_conn_destroy(s_tls);
        s_tls = NULL;
    }
}
