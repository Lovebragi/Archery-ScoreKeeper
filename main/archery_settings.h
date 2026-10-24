// main/archery_settings.h —— 靶型与省电设置（靶子类型、亮度、息屏时间）的纯数据与编解码。
//
// 只放"影响记分方式或功耗、用户能感知"的项：靶子类型、屏幕亮度、空闲息屏时间。
// 息屏时间 0 表示不息屏；其它值为秒数。设置由 archery_store.c 写入 NVS。
#pragma once

#include "archery_score.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 设置页行数：0 = 靶子类型，1 = 屏幕方向，2 = 屏幕亮度，3 = 息屏时间，4 = 返回。
#define ARCHERY_SETTINGS_ROWS 5u

#define ARCHERY_SETTINGS_DEFAULT_BRIGHTNESS 100u
#define ARCHERY_SETTINGS_DEFAULT_OFF_SECONDS 30u
#define ARCHERY_SETTINGS_DEFAULT_TARGET ((uint8_t)ARCHERY_TARGET_HALF_40)
// 默认画面倒转(整机屏幕倒装);与 bsp_pins.h 的 BSP_LCD_ROTATE_180 一致。
#define ARCHERY_SETTINGS_DEFAULT_ROTATE 1u
// v3：magic + 版本 + 靶型 + 方向 + 亮度 + 息屏(2B) + 保留 + CRC。
// 解码兼容 v1（无靶型）与 v2（无方向）格式。
#define ARCHERY_SETTINGS_ENCODED_BYTES 10u
#define ARCHERY_SETTINGS_ENCODED_BYTES_V2 9u
#define ARCHERY_SETTINGS_ENCODED_BYTES_V1 8u

typedef struct {
    uint8_t target;        // archery_target_t
    uint8_t rotate;        // 1 = 画面旋转 180°(默认), 0 = 正常方向
    uint8_t brightness;    // 20/40/60/80/100 (%)
    uint16_t off_seconds;  // 0 = 不息屏；15/30/60/120
} archery_settings_t;

void archery_settings_defaults(archery_settings_t *settings);
bool archery_settings_valid(const archery_settings_t *settings);

// 可选值表（靶型、亮度百分比、息屏秒数），供界面与按键调整使用。
uint8_t archery_settings_target_count(void);
uint8_t archery_settings_target_at(uint8_t index);
uint8_t archery_settings_brightness_count(void);
uint8_t archery_settings_brightness_at(uint8_t index);
uint8_t archery_settings_off_count(void);
uint16_t archery_settings_off_at(uint8_t index);

// 调整某一项：row 0 = 靶型，row 1 = 屏幕方向，row 2 = 亮度，row 3 = 息屏时间。
// 在可选值表上钳制，不回绕；返回值是否真的改了。
bool archery_settings_adjust(archery_settings_t *settings, uint8_t row, int delta);

// 编码/解码（magic + 版本 + 字段 + CRC16），用于 NVS 中的一条 blob。
// 解码兼容旧版 8 字节格式（靶型按 40 半靶处理）。
size_t archery_settings_encode(const archery_settings_t *settings,
                               uint8_t *buffer, size_t capacity);
bool archery_settings_decode(archery_settings_t *settings,
                             const uint8_t *buffer, size_t length);
