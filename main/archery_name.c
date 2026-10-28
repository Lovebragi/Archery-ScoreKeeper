// main/archery_name.c —— 首页名称的校验与 NVS 存取。
#include "archery_name.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "archery_name";
static const char *NVS_NS = "archery";
static const char *NVS_KEY = "dname";
static char s_name[ARCHERY_NAME_MAX_BYTES + 1] = ARCHERY_NAME_DEFAULT;

static size_t utf8_chars(const char *text, size_t length, bool *valid) {
    size_t chars = 0;
    for (size_t i = 0; i < length;) {
        const unsigned char c = (unsigned char)text[i];
        size_t step = 0;
        if (c < 0x80u) {
            step = 1;
            if (c < 0x20u || c == 0x7Fu) *valid = false;
        } else if ((c & 0xE0u) == 0xC0u) {
            step = 2;
        } else if ((c & 0xF0u) == 0xE0u) {
            step = 3;
        } else if ((c & 0xF8u) == 0xF0u) {
            step = 4;
        } else {
            *valid = false;
            return chars;
        }
        if (i + step > length) {
            *valid = false;
            return chars;
        }
        for (size_t k = 1; k < step; k++) {
            if (((unsigned char)text[i + k] & 0xC0u) != 0x80u) *valid = false;
        }
        chars++;
        i += step;
    }
    return chars;
}

bool archery_name_valid(const char *text, size_t length) {
    if (!text || length == 0 || length > ARCHERY_NAME_MAX_BYTES) return false;
    bool valid = true;
    const size_t chars = utf8_chars(text, length, &valid);
    return valid && chars > 0 && chars <= ARCHERY_NAME_MAX_CHARS;
}

void archery_name_init(void) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NS, NVS_READONLY, &handle) != ESP_OK) return;
    char stored[ARCHERY_NAME_MAX_BYTES + 1];
    size_t length = sizeof(stored);
    if (nvs_get_str(handle, NVS_KEY, stored, &length) == ESP_OK &&
        archery_name_valid(stored, strlen(stored))) {
        memcpy(s_name, stored, strlen(stored) + 1);
    }
    nvs_close(handle);
}

const char *archery_name_get(void) { return s_name; }

bool archery_name_save(const char *text) {
    if (!text || !archery_name_valid(text, strlen(text))) return false;
    nvs_handle_t handle;
    if (nvs_open(NVS_NS, NVS_READWRITE, &handle) != ESP_OK) return false;
    const esp_err_t error = nvs_set_str(handle, NVS_KEY, text);
    if (error == ESP_OK) (void)nvs_commit(handle);
    nvs_close(handle);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "保存名称失败: %s", esp_err_to_name(error));
        return false;
    }
    memcpy(s_name, text, strlen(text) + 1);
    return true;
}
