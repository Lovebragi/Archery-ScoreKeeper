// main/archery_name.h —— 设备首页名称（配网页可改，NVS 持久化）。
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define ARCHERY_NAME_MAX_CHARS 8u
#define ARCHERY_NAME_MAX_BYTES 24u
#define ARCHERY_NAME_DEFAULT "射箭计分宝"

void archery_name_init(void);
const char *archery_name_get(void);
bool archery_name_valid(const char *text, size_t length);
// 保存并立即生效；失败返回 false（长度/字符非法或 NVS 写入失败）。
bool archery_name_save(const char *text);
