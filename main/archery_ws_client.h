// main/archery_ws_client.h —— 豆包流式识别（WSS）客户端。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 建立 TLS + WebSocket 连接并发送初始化包；appid/token 由 NVS 提供。
bool archery_ws_open(const char *appid, const char *token);

// 发送一帧 PCM（16 kHz/16 bit/单声道），last 表示最后一块。
bool archery_ws_send_audio(const int16_t *pcm, size_t samples, bool last);

// 读取一帧，提取其中的识别文本；返回是否收到最终结果。
bool archery_ws_read(char *text, size_t capacity);

void archery_ws_close(void);
bool archery_ws_connected(void);
