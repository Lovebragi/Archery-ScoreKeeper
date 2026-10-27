// main/archery_logbuf.h —— 内存日志环形缓冲（诊断页用，掉电即失）。
#pragma once

#include <stddef.h>

#define ARCHERY_LOG_LINES 32
#define ARCHERY_LOG_LINE_CHARS 96

void archery_logbuf_init(void);
size_t archery_logbuf_count(void);
// 把最近日志按时间顺序写入 out，行间以 '\n' 分隔。
void archery_logbuf_snapshot(char *out, size_t capacity);
