// main/archery_store.h —— 历史记录与墙上时钟的 NVS 持久化。
//
// 一条记录一个 NVS blob（key "s000".."s299"），序号与槽位按
// (seq-1) % 300 固定映射；另有一条 meta blob 保存 next_seq。启动时全量扫描，
// 可自动修复缺失或损坏的 meta，因此掉电不会让历史列表整体失效。
// 存储布局变化时（schema 版本不同）会清空一次命名空间，由调用方重新载入设置。
#pragma once

#include "archery_clock.h"
#include "archery_history.h"
#include "archery_settings.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    archery_history_meta_t meta;
    // 按 NVS 槽位下标存放，序号见 record.seq；seq == 0 表示该槽位无效。
    archery_record_t records[ARCHERY_HISTORY_CAPACITY];
} archery_history_t;

// 读取全部历史并修复 meta。调用前需已 nvs_flash_init()。
// 布局 schema 变化时会先清空命名空间（历史、设置、时钟都会重置）。
esp_err_t archery_store_load(archery_history_t *history);

// 追加一条记录：自动分配序号、写入 NVS、更新内存与 meta。
esp_err_t archery_store_save(archery_history_t *history, const archery_record_t *record);

// 清空全部历史并重置序号。
esp_err_t archery_store_clear(archery_history_t *history);

// 第 newest_index 新（0 = 最新）的记录；越界或缺失返回 NULL。
const archery_record_t *archery_history_record(const archery_history_t *history,
                                               uint16_t newest_index);

// 筛选后的条数与第 index 条（0 = 最新，按时间倒序）。filter 取
// archery_history_filter_t；now 为当前本地时间。
uint16_t archery_history_visible_count(const archery_history_t *history,
                                       uint8_t filter, uint32_t now);
const archery_record_t *archery_history_visible_record(const archery_history_t *history,
                                                       uint8_t filter, uint32_t now,
                                                       uint16_t index);

// 墙上时钟：设备没有 RTC 电池，时间靠 NVS 记忆 + 运行期间的单调计时器。
esp_err_t archery_store_clock_load(uint32_t *seconds, bool *time_set);
esp_err_t archery_store_clock_save(uint32_t seconds, bool time_set);

// 省电设置（亮度/息屏时间）。读取失败或数据损坏时回落到默认值并返回 ESP_OK；
// NVS 本身打不开时返回错误，但 settings 仍填默认值。
esp_err_t archery_settings_load(archery_settings_t *settings);
esp_err_t archery_settings_save(const archery_settings_t *settings);
