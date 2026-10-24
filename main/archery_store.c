// main/archery_store.c —— 历史记录的 NVS 读写实现。
#include "archery_store.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "archery_store";
static const char *NVS_NAMESPACE = "archery";
static const char *META_KEY = "meta";
static const char *SETTINGS_KEY = "cfg";

static void slot_key(char *buffer, size_t size, uint8_t slot) {
    snprintf(buffer, size, "s%02u", (unsigned)slot);
}

static esp_err_t open_nvs(nvs_handle_t *handle) {
    return nvs_open(NVS_NAMESPACE, NVS_READWRITE, handle);
}

static esp_err_t write_meta(nvs_handle_t handle, const archery_history_meta_t *meta) {
    return nvs_set_blob(handle, META_KEY, meta, sizeof(*meta));
}

static void history_clear_memory(archery_history_t *history) {
    memset(history, 0, sizeof(*history));
    archery_history_meta_init(&history->meta);
}

esp_err_t archery_store_load(archery_history_t *history) {
    if (!history) return ESP_ERR_INVALID_ARG;
    history_clear_memory(history);

    nvs_handle_t handle;
    esp_err_t err = open_nvs(&handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开 NVS 命名空间失败: %s", esp_err_to_name(err));
        return err;
    }

    // meta 缺失或长度不符时按空历史处理，后面的扫描会重新推算 next_seq。
    archery_history_meta_t meta;
    size_t meta_size = sizeof(meta);
    err = nvs_get_blob(handle, META_KEY, &meta, &meta_size);
    bool meta_ok = err == ESP_OK && meta_size == sizeof(meta) &&
                   meta.next_seq >= 1 &&
                   meta.count <= ARCHERY_HISTORY_CAPACITY;
    if (!meta_ok) {
        archery_history_meta_init(&meta);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "读取 meta 失败(%s)，按空历史处理", esp_err_to_name(err));
        }
    }

    uint8_t buffer[ARCHERY_RECORD_BUFFER_BYTES];
    uint16_t max_seq = 0;
    for (uint8_t slot = 0; slot < ARCHERY_HISTORY_CAPACITY; slot++) {
        char key[8];
        slot_key(key, sizeof(key), slot);
        size_t length = sizeof(buffer);
        esp_err_t read_err = nvs_get_blob(handle, key, buffer, &length);
        archery_record_t record;
        if (read_err == ESP_OK && archery_record_decode(&record, buffer, length)) {
            history->records[slot] = record;
            if (record.seq > max_seq) max_seq = record.seq;
        } else {
            history->records[slot].seq = 0;
            if (read_err != ESP_OK && read_err != ESP_ERR_NVS_NOT_FOUND) {
                ESP_LOGW(TAG, "槽位 %u 读取失败: %s", (unsigned)slot,
                         esp_err_to_name(read_err));
            } else if (read_err == ESP_OK) {
                ESP_LOGW(TAG, "槽位 %u 记录校验失败，已忽略", (unsigned)slot);
            }
        }
    }

    bool dirty = !meta_ok;
    if (meta.next_seq <= max_seq) {
        meta.next_seq = (uint16_t)(max_seq + 1u);
        dirty = true;
    }

    // 从最新一条向前数出连续可用的记录数；遇到缺口即视为更早的记录已不可用。
    uint16_t count = 0;
    for (uint16_t i = 0; i < ARCHERY_HISTORY_CAPACITY; i++) {
        const uint16_t seq = archery_history_seq_newest(&meta, i);
        if (seq == 0) break;
        const uint8_t slot = archery_history_slot_for_seq(seq);
        if (slot == 0xFFu || history->records[slot].seq != seq) break;
        count++;
    }
    if (meta.count != count) {
        meta.count = count;
        dirty = true;
    }

    history->meta = meta;
    if (dirty) {
        esp_err_t write_err = write_meta(handle, &meta);
        if (write_err == ESP_OK) write_err = nvs_commit(handle);
        if (write_err != ESP_OK) {
            ESP_LOGW(TAG, "修复 meta 写回失败: %s", esp_err_to_name(write_err));
        }
    }
    nvs_close(handle);
    ESP_LOGI(TAG, "历史记录载入 %u 条，下一条序号 %u", (unsigned)count,
             (unsigned)meta.next_seq);
    return ESP_OK;
}

esp_err_t archery_store_save(archery_history_t *history, const archery_record_t *record) {
    if (!history || !record) return ESP_ERR_INVALID_ARG;
    if (history->meta.next_seq == 0) return ESP_ERR_INVALID_STATE;

    archery_record_t stored = *record;
    stored.seq = history->meta.next_seq;

    uint8_t buffer[ARCHERY_RECORD_BUFFER_BYTES];
    const size_t length = archery_record_encode(&stored, buffer, sizeof(buffer));
    if (length == 0) return ESP_ERR_INVALID_ARG;

    const uint8_t slot = archery_history_slot_for_seq(stored.seq);
    if (slot == 0xFFu) return ESP_ERR_INVALID_STATE;

    nvs_handle_t handle;
    esp_err_t err = open_nvs(&handle);
    if (err != ESP_OK) return err;

    char key[8];
    slot_key(key, sizeof(key), slot);
    err = nvs_set_blob(handle, key, buffer, length);
    if (err == ESP_OK) {
        archery_history_meta_t meta = history->meta;
        archery_history_meta_after_save(&meta);
        err = write_meta(handle, &meta);
        if (err == ESP_OK) err = nvs_commit(handle);
        if (err == ESP_OK) {
            history->records[slot] = stored;
            history->meta = meta;
        }
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "写入记录失败: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "记录 #%u 已保存: %u 组 × %u 箭", (unsigned)stored.seq,
             (unsigned)stored.groups, (unsigned)stored.arrows);
    return ESP_OK;
}

esp_err_t archery_store_clear(archery_history_t *history) {
    if (!history) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = open_nvs(&handle);
    if (err != ESP_OK) return err;
    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        archery_history_meta_t meta;
        archery_history_meta_init(&meta);
        err = write_meta(handle, &meta);
        if (err == ESP_OK) err = nvs_commit(handle);
        if (err == ESP_OK) history_clear_memory(history);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "清空历史失败: %s", esp_err_to_name(err));
    }
    return err;
}

const archery_record_t *archery_history_record(const archery_history_t *history,
                                               uint16_t newest_index) {
    if (!history) return NULL;
    const uint16_t seq = archery_history_seq_newest(&history->meta, newest_index);
    if (seq == 0) return NULL;
    const uint8_t slot = archery_history_slot_for_seq(seq);
    if (slot == 0xFFu) return NULL;
    const archery_record_t *record = &history->records[slot];
    return record->seq == seq ? record : NULL;
}

esp_err_t archery_settings_load(archery_settings_t *settings) {
    if (!settings) return ESP_ERR_INVALID_ARG;
    archery_settings_defaults(settings);

    nvs_handle_t handle;
    esp_err_t err = open_nvs(&handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "打开 NVS 读取设置失败(%s)，使用默认值", esp_err_to_name(err));
        return err;
    }
    uint8_t buffer[ARCHERY_SETTINGS_ENCODED_BYTES];
    size_t length = sizeof(buffer);
    err = nvs_get_blob(handle, SETTINGS_KEY, buffer, &length);
    archery_settings_t loaded;
    if (err == ESP_OK && archery_settings_decode(&loaded, buffer, length)) {
        *settings = loaded;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "设置数据不可用(%s)，使用默认值", esp_err_to_name(err));
    }
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t archery_settings_save(const archery_settings_t *settings) {
    if (!settings) return ESP_ERR_INVALID_ARG;
    uint8_t buffer[ARCHERY_SETTINGS_ENCODED_BYTES];
    const size_t length = archery_settings_encode(settings, buffer, sizeof(buffer));
    if (length == 0) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = open_nvs(&handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, SETTINGS_KEY, buffer, length);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "保存设置失败: %s", esp_err_to_name(err));
    }
    return err;
}
