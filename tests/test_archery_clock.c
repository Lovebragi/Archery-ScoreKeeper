// tests/test_archery_clock.c —— 本地时钟换算、格式化与历史时间筛选测试。
#include "archery_clock.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t seconds_of(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi,
                           uint8_t s) {
    const archery_datetime_t dt = {y, mo, d, h, mi, s};
    assert(archery_datetime_valid(&dt));
    return archery_datetime_to_seconds(&dt);
}

static void test_days_in_month(void) {
    assert(archery_clock_days_in_month(2000, 2) == 29);  // 2000 是闰年
    assert(archery_clock_days_in_month(2001, 2) == 28);
    assert(archery_clock_days_in_month(2024, 2) == 29);
    assert(archery_clock_days_in_month(2099, 2) == 28);
    assert(archery_clock_days_in_month(2026, 4) == 30);
    assert(archery_clock_days_in_month(2026, 12) == 31);
    assert(archery_clock_days_in_month(2026, 0) == 0);
    assert(archery_clock_days_in_month(2026, 13) == 0);
}

static void test_epoch_and_roundtrip(void) {
    assert(seconds_of(2000, 1, 1, 0, 0, 0) == 0);
    assert(seconds_of(2000, 1, 2, 0, 0, 0) == 86400);
    assert(seconds_of(2000, 2, 1, 0, 0, 0) == 31u * 86400u);
    assert(seconds_of(2000, 3, 1, 0, 0, 0) == (31u + 29u) * 86400u);

    const uint32_t when = seconds_of(2026, 10, 25, 6, 32, 18);
    archery_datetime_t dt;
    archery_seconds_to_datetime(when, &dt);
    assert(dt.year == 2026 && dt.month == 10 && dt.day == 25);
    assert(dt.hour == 6 && dt.minute == 32 && dt.second == 18);

    const uint32_t last = seconds_of(2099, 12, 31, 23, 59, 59);
    archery_seconds_to_datetime(last, &dt);
    assert(dt.year == 2099 && dt.month == 12 && dt.day == 31);
    assert(dt.hour == 23 && dt.minute == 59 && dt.second == 59);
}

static void test_validation(void) {
    archery_datetime_t dt = {2026, 10, 25, 6, 32, 0};
    assert(archery_datetime_valid(&dt));
    dt.month = 13;
    assert(!archery_datetime_valid(&dt));
    dt = (archery_datetime_t){2026, 2, 29, 0, 0, 0};
    assert(!archery_datetime_valid(&dt));
    dt = (archery_datetime_t){2024, 2, 29, 0, 0, 0};
    assert(archery_datetime_valid(&dt));
    dt = (archery_datetime_t){1999, 1, 1, 0, 0, 0};
    assert(!archery_datetime_valid(&dt));
    dt = (archery_datetime_t){2026, 1, 1, 24, 0, 0};
    assert(!archery_datetime_valid(&dt));

    archery_datetime_t bad = {2026, 2, 30, 0, 0, 0};
    assert(archery_datetime_to_seconds(&bad) == 0);
}

static void test_format(void) {
    char buffer[24];
    const uint32_t when = seconds_of(2026, 10, 25, 14, 32, 0);
    archery_clock_format_short(when, buffer, sizeof(buffer));
    assert(strcmp(buffer, "10-25 14:32") == 0);
    archery_clock_format_full(when, buffer, sizeof(buffer));
    assert(strcmp(buffer, "2026-10-25 14:32") == 0);
    archery_clock_format_short(0, buffer, sizeof(buffer));
    assert(strcmp(buffer, "--") == 0);
}

static void test_day_and_week(void) {
    const uint32_t now = seconds_of(2026, 10, 25, 14, 32, 0);
    const uint32_t today = seconds_of(2026, 10, 25, 0, 0, 0);
    assert(archery_clock_day_start(now) == today);
    assert(archery_clock_week_start(now) ==
           today - 6u * ARCHERY_CLOCK_SECONDS_PER_DAY);
    assert(archery_clock_same_day(now, today + 3600));
    assert(!archery_clock_same_day(now, today - 60));

    // 筛选：全部 / 今天 / 最近 7 天
    assert(archery_clock_matches_filter(now, ARCHERY_HISTORY_FILTER_ALL, now));
    assert(archery_clock_matches_filter(now, ARCHERY_HISTORY_FILTER_TODAY, now));
    assert(archery_clock_matches_filter(now, ARCHERY_HISTORY_FILTER_WEEK, now));
    const uint32_t yesterday = seconds_of(2026, 10, 24, 20, 0, 0);
    assert(!archery_clock_matches_filter(yesterday, ARCHERY_HISTORY_FILTER_TODAY, now));
    assert(archery_clock_matches_filter(yesterday, ARCHERY_HISTORY_FILTER_WEEK, now));
    const uint32_t long_ago = seconds_of(2026, 10, 1, 8, 0, 0);
    assert(!archery_clock_matches_filter(long_ago, ARCHERY_HISTORY_FILTER_WEEK, now));
    // 时间未知的记录只在"全部"下出现
    assert(archery_clock_matches_filter(0, ARCHERY_HISTORY_FILTER_ALL, now));
    assert(!archery_clock_matches_filter(0, ARCHERY_HISTORY_FILTER_TODAY, now));
    assert(!archery_clock_matches_filter(0, ARCHERY_HISTORY_FILTER_WEEK, now));
}

static void test_parse_build(void) {
    archery_datetime_t dt;
    assert(archery_clock_parse_build("Oct 25 2026", "07:23:54", &dt));
    assert(dt.year == 2026 && dt.month == 10 && dt.day == 25);
    assert(dt.hour == 7 && dt.minute == 23 && dt.second == 54);
    // 编译器对个位数日期补空格："Oct  5 2026"
    assert(archery_clock_parse_build("Oct  5 2026", "00:01:02", &dt));
    assert(dt.day == 5);
    assert(!archery_clock_parse_build("Foo 25 2026", "07:23:54", &dt));
    assert(!archery_clock_parse_build("Oct 25 2026", "7:23", &dt));
}

int main(void) {
    test_days_in_month();
    test_epoch_and_roundtrip();
    test_validation();
    test_format();
    test_day_and_week();
    test_parse_build();
    puts("archery clock tests: PASS");
    return 0;
}
