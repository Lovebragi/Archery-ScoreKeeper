// main/archery_score.h —— 靶型、环值定义与纯逻辑换算。
//
// 支持的靶型：
//   40 半靶 / 三联靶：靶面只印 6~10 环，10 环内有 X（内十），脱靶记 0；
//   40 全靶 / 80 全靶 / 122 全靶：0~10 环 + X，0 环就是脱靶。
// 所有环值统一编码为 0..10（0 = miss）与 11（X），4 bit 即可容纳，便于历史
// 记录压缩。此文件不依赖 ESP-IDF/LVGL，可直接在主机上单测。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 环值编码：0 = 脱靶/0 环，1..10 = 环数，11 = X（内十，按 10 环计分）。
#define ARCHERY_SCORE_MISS      0u
#define ARCHERY_SCORE_RING_MIN  1u
#define ARCHERY_SCORE_RING_MAX  10u
#define ARCHERY_SCORE_X         11u

// 录入槽位上限（全靶：X + 10..0 共 12 个）。实际槽位数由靶型决定。
#define ARCHERY_RING_SLOT_MAX 12u
#define ARCHERY_RING_SLOT_INVALID 0xFFu

typedef enum {
    ARCHERY_TARGET_HALF_40 = 0,  // 40 半靶：X、10~6、脱靶
    ARCHERY_TARGET_TRIPLE_40H,   // 三联靶：上中下三个 40 半靶
    ARCHERY_TARGET_FULL_40,      // 40 全靶：X、10~0
    ARCHERY_TARGET_FULL_80,      // 80 全靶
    ARCHERY_TARGET_FULL_122,     // 122 全靶
    ARCHERY_TARGET_COUNT,
} archery_target_t;

bool archery_target_valid(uint8_t target);

// 该靶型的录入槽位数与槽位对应的环值编码（0 表示 miss）。
uint8_t archery_target_slot_count(uint8_t target);
uint8_t archery_target_slot_code(uint8_t target, uint8_t slot);

// 环值是否合法。0..10 与 X 都合法；越界值按"数据损坏"处理。
bool archery_score_valid(uint8_t code);

// 计入总环数的分值：0 环 = 0，1..10 原值，X = 10。
uint8_t archery_score_points(uint8_t code);

// 界面文字："M"、"1"…"10"、"X"。只含 ASCII，用 Montserrat / 数字字体渲染。
const char *archery_score_text(uint8_t code);

bool archery_score_is_x(uint8_t code);
bool archery_score_is_miss(uint8_t code);
