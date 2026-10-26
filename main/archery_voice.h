// main/archery_voice.h —— 实验功能：按住上键说话，整组报靶。
//
// 设备把 16 kHz/16 bit 单声道 PCM 通过 HTTP chunked 直传局域网代理，
// 代理负责云端识别与结构化，返回固定 JSON；本模块只做传输和最小解析。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define ARCHERY_VOICE_MAX_ITEMS 12

typedef struct {
    uint8_t ring;   // 0..10，或 11 = X（内十）
    uint8_t count;  // 箭数；all 时由设备按剩余数填充
    bool all;
} archery_voice_item_t;

typedef struct {
    bool ok;
    uint8_t count;
    archery_voice_item_t items[ARCHERY_VOICE_MAX_ITEMS];
    char error[24];
} archery_voice_result_t;

// 从 NVS 读取语音服务地址；未配置时功能关闭。
esp_err_t archery_voice_init(void);
bool archery_voice_enabled(void);
const char *archery_voice_url(void);
esp_err_t archery_voice_save_url(const char *url);

// PTT：按下（长按触发）与松开。
esp_err_t archery_voice_start(void);
void archery_voice_stop(void);
bool archery_voice_active(void);

// 本次识别使用的靶型与箭数（代理据此解析与校验）。
void archery_voice_set_context(uint8_t target, uint8_t arrows);
uint8_t archery_voice_context_target(void);
uint8_t archery_voice_context_arrows(void);

// 直连豆包流式识别所需的凭据（AppID + Access Token），存 NVS。
esp_err_t archery_voice_save_credentials(const char *appid, const char *token);
const char *archery_voice_appid(void);
const char *archery_voice_token(void);
bool archery_voice_has_credentials(void);

// 识别完成的结果（消费一次）。
bool archery_voice_take_result(archery_voice_result_t *out);

// 录入页显示的状态文字；NULL 表示不显示。
const char *archery_voice_status(void);
void archery_voice_set_status(const char *text);
