// main/archery_speech.h —— 报靶文本解析：把 ASR 文本变成固定环值条目。
//
// 纯逻辑，不依赖 ESP-IDF/LVGL，可在主机上单测。词表与规则见
// .publish/speech-parser/archery_speech.py（Python 原型）。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ARCHERY_SPEECH_MAX_ITEMS 12

typedef struct {
    uint8_t ring;   // 0..10，11 = X（内十）
    uint8_t count;  // 箭数；all 时由设备按剩余数填充
    bool all;
} archery_speech_item_t;

typedef enum {
    ARCHERY_SPEECH_OK = 0,
    ARCHERY_SPEECH_UNPARSED,     // 没有任何可识别的内容
    ARCHERY_SPEECH_INCOMPLETE,   // 缺数量或环值
    ARCHERY_SPEECH_RING_INVALID, // 环值不属于当前靶型
    ARCHERY_SPEECH_MISMATCH,     // 数量之和与本组箭数不符
    ARCHERY_SPEECH_EXCESS,       // 数量之和超过本组箭数
    ARCHERY_SPEECH_MULTI_ALL,    // 出现多个"全是/剩下都是"
    ARCHERY_SPEECH_AMBIGUOUS,    // 裸数字无法唯一分配
} archery_speech_error_t;

typedef struct {
    bool ok;
    uint8_t count;
    archery_speech_item_t items[ARCHERY_SPEECH_MAX_ITEMS];
    uint8_t arrows;
    uint8_t missing;
    archery_speech_error_t error;
} archery_speech_result_t;

// target 使用 archery_target_t；arrows 为本组（或当前靶面）箭数。
void archery_speech_parse(const char *text, uint8_t target, uint8_t arrows,
                          archery_speech_result_t *out);
