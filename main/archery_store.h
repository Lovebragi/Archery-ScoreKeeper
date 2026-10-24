// main/archery_store.h —— 历史记录的 NVS 持久化。
//
// 一条记录一个 NVS blob（key "s00".."s49"），序号与槽位按 (seq-1) % 50 固定
// 映射；另有一条 meta blob 保存 next_seq。启动时全量扫描，可自动修复缺失或
// 损坏的 meta，因此掉电不会让历史列表整体失效。
#pragma once

#include "archery_history.h"
#include "archery_settings.h"
#include "esp_err.h"
#include <stdbool.h>

typedef struct {
    archery_history_meta_t meta;
    // 按 NVS 槽位下标存放，序号见 record.seq；seq == 0 表示该槽位无效。
    archery_record_t records[ARCHERY_HISTORY_CAPACITY];
} archery_history_t;

// 读取全部历史并修复 meta。调用前需已 nvs_flash_init()。
esp_err_t archery_store_load(archery_history_t *history);

// 追加一条记录：自动分配序号、写入 NVS、更新内存与 meta。
esp_err_t archery_store_save(archery_history_t *history, const archery_record_t *record);

// 清空全部历史并重置序号。
esp_err_t archery_store_clear(archery_history_t *history);

// 第 newest_index 新（0 = 最新）的记录；越界或缺失返回 NULL。
const archery_record_t *archery_history_record(const archery_history_t *history,
                                               uint16_t newest_index);

// 省电设置（亮度/息屏时间）。读取失败或数据损坏时回落到默认值并返回 ESP_OK；
// NVS 本身打不开时返回错误，但 settings 仍填默认值。
esp_err_t archery_settings_load(archery_settings_t *settings);
esp_err_t archery_settings_save(const archery_settings_t *settings);
