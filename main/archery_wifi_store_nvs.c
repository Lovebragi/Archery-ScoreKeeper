// main/archery_wifi_store_nvs.c —— 已保存 Wi-Fi 列表的 NVS 读写。
#include "archery_wifi_store.h"

#include <string.h>

#include "nvs.h"

#define NVS_NAMESPACE "archery"
#define NVS_KEY "w_list"

bool archery_wifi_store_load(archery_wifi_store_t *store) {
    if (!store) return false;
    archery_wifi_store_reset(store);
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
    uint8_t buffer[ARCHERY_WIFI_STORE_BYTES];
    size_t length = sizeof(buffer);
    const esp_err_t err = nvs_get_blob(handle, NVS_KEY, buffer, &length);
    nvs_close(handle);
    return err == ESP_OK && archery_wifi_store_decode(store, buffer, length);
}

bool archery_wifi_store_save(const archery_wifi_store_t *store) {
    if (!store) return false;
    uint8_t buffer[ARCHERY_WIFI_STORE_BYTES];
    const size_t length = archery_wifi_store_encode(store, buffer, sizeof(buffer));
    if (length == 0) return false;
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
    const esp_err_t err = nvs_set_blob(handle, NVS_KEY, buffer, length);
    if (err == ESP_OK) (void)nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

void archery_wifi_store_clear(void) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    (void)nvs_erase_key(handle, NVS_KEY);
    (void)nvs_erase_key(handle, "w_ssid");
    (void)nvs_erase_key(handle, "w_pass");
    (void)nvs_commit(handle);
    nvs_close(handle);
}

void archery_wifi_store_remember(const char *ssid, const char *pass) {
    if (!ssid || !ssid[0]) return;
    archery_wifi_store_t store;
    if (!archery_wifi_store_load(&store)) archery_wifi_store_reset(&store);
    if (archery_wifi_store_upsert(&store, ssid, pass)) {
        (void)archery_wifi_store_save(&store);
    }
}

bool archery_wifi_store_lookup(const char *ssid, char *pass, size_t capacity) {
    if (!ssid) return false;
    archery_wifi_store_t store;
    if (!archery_wifi_store_load(&store)) return false;
    return archery_wifi_store_find(&store, ssid, pass, capacity);
}
