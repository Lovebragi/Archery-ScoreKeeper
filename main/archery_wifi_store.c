// main/archery_wifi_store.c —— 已保存 Wi-Fi 列表的纯逻辑。
#include "archery_wifi_store.h"

#include <string.h>

#define STORE_MAGIC 0x57u
#define STORE_VERSION 1u
#define ENTRY_BYTES (ARCHERY_WIFI_SSID_SIZE + ARCHERY_WIFI_PASS_SIZE + 2u)
#define DATA_BYTES (3u + ARCHERY_WIFI_STORE_MAX * ENTRY_BYTES)

static uint16_t crc16_ccitt(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < length; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

void archery_wifi_store_reset(archery_wifi_store_t *store) {
    if (!store) return;
    memset(store, 0, sizeof(*store));
}

static void copy_text(char *dst, size_t size, const char *src) {
    strncpy(dst, src ? src : "", size - 1);
    dst[size - 1] = '\0';
}

bool archery_wifi_store_upsert(archery_wifi_store_t *store, const char *ssid,
                               const char *pass) {
    if (!store || !ssid || !ssid[0]) return false;
    const uint16_t used = ++store->next_used;

    for (uint8_t i = 0; i < store->count; i++) {
        if (strcmp(store->items[i].ssid, ssid) == 0) {
            copy_text(store->items[i].pass, ARCHERY_WIFI_PASS_SIZE, pass);
            store->items[i].used = used;
            return true;
        }
    }

    uint8_t slot = store->count;
    if (slot >= ARCHERY_WIFI_STORE_MAX) {
        slot = 0;
        for (uint8_t i = 1; i < ARCHERY_WIFI_STORE_MAX; i++) {
            if (store->items[i].used < store->items[slot].used) slot = i;
        }
    } else {
        store->count++;
    }
    memset(&store->items[slot], 0, sizeof(store->items[slot]));
    copy_text(store->items[slot].ssid, ARCHERY_WIFI_SSID_SIZE, ssid);
    copy_text(store->items[slot].pass, ARCHERY_WIFI_PASS_SIZE, pass);
    store->items[slot].used = used;
    return true;
}

bool archery_wifi_store_find(const archery_wifi_store_t *store, const char *ssid,
                             char *pass, size_t capacity) {
    if (!store || !ssid || !pass || capacity == 0) return false;
    for (uint8_t i = 0; i < store->count; i++) {
        if (strcmp(store->items[i].ssid, ssid) == 0) {
            copy_text(pass, capacity, store->items[i].pass);
            return true;
        }
    }
    return false;
}

size_t archery_wifi_store_encode(const archery_wifi_store_t *store, uint8_t *buffer,
                                 size_t capacity) {
    if (!store || !buffer || capacity < ARCHERY_WIFI_STORE_BYTES) return 0;
    memset(buffer, 0, ARCHERY_WIFI_STORE_BYTES);
    buffer[0] = STORE_MAGIC;
    buffer[1] = STORE_VERSION;
    buffer[2] = store->count;
    size_t offset = 3;
    for (uint8_t i = 0; i < ARCHERY_WIFI_STORE_MAX; i++) {
        if (i < store->count) {
            memcpy(buffer + offset, store->items[i].ssid, ARCHERY_WIFI_SSID_SIZE);
            memcpy(buffer + offset + ARCHERY_WIFI_SSID_SIZE, store->items[i].pass,
                   ARCHERY_WIFI_PASS_SIZE);
            buffer[offset + ARCHERY_WIFI_SSID_SIZE + ARCHERY_WIFI_PASS_SIZE] =
                (uint8_t)(store->items[i].used & 0xFFu);
            buffer[offset + ARCHERY_WIFI_SSID_SIZE + ARCHERY_WIFI_PASS_SIZE + 1] =
                (uint8_t)(store->items[i].used >> 8);
        }
        offset += ENTRY_BYTES;
    }
    const uint16_t crc = crc16_ccitt(buffer, DATA_BYTES);
    buffer[DATA_BYTES] = (uint8_t)(crc & 0xFFu);
    buffer[DATA_BYTES + 1] = (uint8_t)(crc >> 8);
    return ARCHERY_WIFI_STORE_BYTES;
}

bool archery_wifi_store_decode(archery_wifi_store_t *store, const uint8_t *buffer,
                               size_t length) {
    if (!store || !buffer || length != ARCHERY_WIFI_STORE_BYTES) return false;
    if (buffer[0] != STORE_MAGIC || buffer[1] != STORE_VERSION) return false;
    const uint16_t expected = (uint16_t)(buffer[DATA_BYTES] |
                                         ((uint16_t)buffer[DATA_BYTES + 1] << 8));
    if (crc16_ccitt(buffer, DATA_BYTES) != expected) return false;
    const uint8_t count = buffer[2];
    if (count > ARCHERY_WIFI_STORE_MAX) return false;

    archery_wifi_store_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    decoded.count = count;
    size_t offset = 3;
    for (uint8_t i = 0; i < count; i++) {
        memcpy(decoded.items[i].ssid, buffer + offset, ARCHERY_WIFI_SSID_SIZE);
        memcpy(decoded.items[i].pass, buffer + offset + ARCHERY_WIFI_SSID_SIZE,
               ARCHERY_WIFI_PASS_SIZE);
        decoded.items[i].ssid[ARCHERY_WIFI_SSID_SIZE - 1] = '\0';
        decoded.items[i].pass[ARCHERY_WIFI_PASS_SIZE - 1] = '\0';
        decoded.items[i].used =
            (uint16_t)(buffer[offset + ARCHERY_WIFI_SSID_SIZE + ARCHERY_WIFI_PASS_SIZE] |
                       ((uint16_t)buffer[offset + ARCHERY_WIFI_SSID_SIZE +
                                             ARCHERY_WIFI_PASS_SIZE + 1]
                        << 8));
        if (decoded.items[i].used > decoded.next_used) {
            decoded.next_used = decoded.items[i].used;
        }
        offset += ENTRY_BYTES;
    }
    *store = decoded;
    return true;
}
