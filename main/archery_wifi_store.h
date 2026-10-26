// main/archery_wifi_store.h —— 已保存 Wi-Fi 列表（最多 10 条）。
//
// 纯逻辑（去重、淘汰、编解码）与 NVS 读写分开，便于主机测试。
// 注意：密码以可读形式保存在 NVS，未做硬件级加密；设置里可关闭保存。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ARCHERY_WIFI_STORE_MAX 10u
#define ARCHERY_WIFI_SSID_SIZE 33u
#define ARCHERY_WIFI_PASS_SIZE 65u
#define ARCHERY_WIFI_STORE_BYTES (3u + ARCHERY_WIFI_STORE_MAX * 100u + 2u)

typedef struct {
    char ssid[ARCHERY_WIFI_SSID_SIZE];
    char pass[ARCHERY_WIFI_PASS_SIZE];
    uint16_t used;  // 最近使用序号，越大越新
} archery_wifi_entry_t;

typedef struct {
    uint8_t count;
    uint16_t next_used;
    archery_wifi_entry_t items[ARCHERY_WIFI_STORE_MAX];
} archery_wifi_store_t;

void archery_wifi_store_reset(archery_wifi_store_t *store);

// 记住一个网络：同 SSID 覆盖密码并刷新使用序号；满了淘汰最久未用的。
bool archery_wifi_store_upsert(archery_wifi_store_t *store, const char *ssid,
                               const char *pass);

// 查找已保存的密码；找到返回 true 并复制到 pass。
bool archery_wifi_store_find(const archery_wifi_store_t *store, const char *ssid,
                             char *pass, size_t capacity);

size_t archery_wifi_store_encode(const archery_wifi_store_t *store, uint8_t *buffer,
                                 size_t capacity);
bool archery_wifi_store_decode(archery_wifi_store_t *store, const uint8_t *buffer,
                               size_t length);

// NVS 读写（命名空间 archery，键 w_list）。
bool archery_wifi_store_load(archery_wifi_store_t *store);
bool archery_wifi_store_save(const archery_wifi_store_t *store);
void archery_wifi_store_clear(void);

// 便捷接口：记住/查找/清空当前保存的网络。关闭保存时由调用方跳过。
void archery_wifi_store_remember(const char *ssid, const char *pass);
bool archery_wifi_store_lookup(const char *ssid, char *pass, size_t capacity);
