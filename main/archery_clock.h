// main/archery_clock.h —— 无时区、无网络的本地墙上时钟（纯逻辑，可主机单测）。
//
// 设备没有带电池的 RTC：断电后时间会丢失。应用在内存里维护"基准时间 + 单调
// 计时器"的时钟，定期把基准写进 NVS；开机时从上次保存的时间继续。设置页可
// 手动校准。这里只负责公历换算、格式化和"今天/最近 7 天"判断，不碰硬件。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 以 2000-01-01 00:00:00 为 0 秒；年份范围 2000..2099，uint32 足够。
#define ARCHERY_CLOCK_EPOCH_YEAR 2000u
#define ARCHERY_CLOCK_MIN_YEAR 2000u
#define ARCHERY_CLOCK_MAX_YEAR 2099u
#define ARCHERY_CLOCK_SECONDS_PER_DAY 86400u

typedef struct {
    uint16_t year;  // 2000..2099
    uint8_t month;  // 1..12
    uint8_t day;    // 1..当月天数
    uint8_t hour;   // 0..23
    uint8_t minute; // 0..59
    uint8_t second; // 0..59
} archery_datetime_t;

// 历史筛选：全部 / 今天 / 最近 7 天（含今天）。
typedef enum {
    ARCHERY_HISTORY_FILTER_ALL = 0,
    ARCHERY_HISTORY_FILTER_TODAY,
    ARCHERY_HISTORY_FILTER_WEEK,
    ARCHERY_HISTORY_FILTER_COUNT,
} archery_history_filter_t;

uint8_t archery_clock_days_in_month(uint16_t year, uint8_t month);
bool archery_datetime_valid(const archery_datetime_t *dt);

// 时间戳与字段互转；字段非法时 from_fields 返回 0，to_fields 输出 2000-01-01。
uint32_t archery_datetime_to_seconds(const archery_datetime_t *dt);
void archery_seconds_to_datetime(uint32_t seconds, archery_datetime_t *dt);

// 当天零点与"最近 7 天"起点（含今天）。
uint32_t archery_clock_day_start(uint32_t seconds);
uint32_t archery_clock_week_start(uint32_t seconds);
bool archery_clock_same_day(uint32_t a, uint32_t b);

// 记录时间是否满足筛选条件。时间未知（0）只在"全部"下显示。
bool archery_clock_matches_filter(uint32_t when, uint8_t filter, uint32_t now);

// 格式化："10-25 14:32" 与 "2026-10-25 14:32"；时间未知输出 "--"。
void archery_clock_format_short(uint32_t seconds, char *buffer, size_t size);
void archery_clock_format_full(uint32_t seconds, char *buffer, size_t size);

// 解析编译器内置的 __DATE__ / __TIME__（如 "Oct 25 2026" / "07:23:54"），
// 用作首次开机的近似种子时间。格式不符返回 false。
bool archery_clock_parse_build(const char *date, const char *time,
                               archery_datetime_t *dt);
