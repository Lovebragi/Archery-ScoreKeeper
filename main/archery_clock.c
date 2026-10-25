// main/archery_clock.c —— 公历换算、格式化与历史时间筛选，纯逻辑实现。
#include "archery_clock.h"

#include <stdio.h>
#include <string.h>

static const char *const s_month_names[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};
static const char *const s_month_names_full[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December",
};
static const uint8_t s_days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static bool is_leap_year(uint16_t year) {
    return (year % 4u == 0u && year % 100u != 0u) || year % 400u == 0u;
}

uint8_t archery_clock_days_in_month(uint16_t year, uint8_t month) {
    if (month < 1u || month > 12u) return 0;
    if (month == 2u && is_leap_year(year)) return 29;
    return s_days_in_month[month - 1u];
}

bool archery_datetime_valid(const archery_datetime_t *dt) {
    if (!dt) return false;
    if (dt->year < ARCHERY_CLOCK_MIN_YEAR || dt->year > ARCHERY_CLOCK_MAX_YEAR) return false;
    if (dt->month < 1u || dt->month > 12u) return false;
    if (dt->day < 1u || dt->day > archery_clock_days_in_month(dt->year, dt->month)) {
        return false;
    }
    if (dt->hour > 23u || dt->minute > 59u || dt->second > 59u) return false;
    return true;
}

// Howard Hinnant 的 civil_from_days / days_from_civil（公历，proleptic）。
static int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2u;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2u ? (unsigned)-3 : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const int yy = (int)yoe + (int)(era * 400);
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    const unsigned dd = doy - (153u * mp + 2u) / 5u + 1u;
    const unsigned mm = mp + (mp < 10u ? 3u : (unsigned)-9);
    *y = yy + (mm <= 2u ? 1 : 0);
    *m = mm;
    *d = dd;
}

#define EPOCH_DAYS (days_from_civil((int)ARCHERY_CLOCK_EPOCH_YEAR, 1u, 1u))

uint32_t archery_datetime_to_seconds(const archery_datetime_t *dt) {
    if (!archery_datetime_valid(dt)) return 0;
    const int64_t days = days_from_civil(dt->year, dt->month, dt->day) - EPOCH_DAYS;
    const int64_t seconds = days * (int64_t)ARCHERY_CLOCK_SECONDS_PER_DAY +
                            (int64_t)dt->hour * 3600 + dt->minute * 60 + dt->second;
    if (seconds < 0) return 0;
    return (uint32_t)seconds;
}

void archery_seconds_to_datetime(uint32_t seconds, archery_datetime_t *dt) {
    if (!dt) return;
    const int64_t days = (int64_t)(seconds / ARCHERY_CLOCK_SECONDS_PER_DAY);
    const uint32_t rest = seconds % ARCHERY_CLOCK_SECONDS_PER_DAY;
    int y = 0;
    unsigned m = 0, d = 0;
    civil_from_days(EPOCH_DAYS + days, &y, &m, &d);
    dt->year = (uint16_t)y;
    dt->month = (uint8_t)m;
    dt->day = (uint8_t)d;
    dt->hour = (uint8_t)(rest / 3600u);
    dt->minute = (uint8_t)((rest % 3600u) / 60u);
    dt->second = (uint8_t)(rest % 60u);
}

uint32_t archery_clock_day_start(uint32_t seconds) {
    return (uint32_t)((seconds / ARCHERY_CLOCK_SECONDS_PER_DAY) *
                      ARCHERY_CLOCK_SECONDS_PER_DAY);
}

uint32_t archery_clock_week_start(uint32_t seconds) {
    const uint32_t day = archery_clock_day_start(seconds);
    const uint32_t six_days = 6u * ARCHERY_CLOCK_SECONDS_PER_DAY;
    return day >= six_days ? (uint32_t)(day - six_days) : 0u;
}

bool archery_clock_same_day(uint32_t a, uint32_t b) {
    return archery_clock_day_start(a) == archery_clock_day_start(b);
}

bool archery_clock_matches_filter(uint32_t when, uint8_t filter, uint32_t now) {
    if (filter == ARCHERY_HISTORY_FILTER_ALL) return true;
    if (when == 0) return false;
    if (filter == ARCHERY_HISTORY_FILTER_TODAY) return archery_clock_same_day(when, now);
    if (filter == ARCHERY_HISTORY_FILTER_WEEK) return when >= archery_clock_week_start(now);
    return false;
}

void archery_clock_format_short(uint32_t seconds, char *buffer, size_t size) {
    if (!buffer || size == 0) return;
    if (seconds == 0) {
        snprintf(buffer, size, "--");
        return;
    }
    archery_datetime_t dt;
    archery_seconds_to_datetime(seconds, &dt);
    snprintf(buffer, size, "%02u-%02u %02u:%02u", (unsigned)dt.month, (unsigned)dt.day,
             (unsigned)dt.hour, (unsigned)dt.minute);
}

void archery_clock_format_full(uint32_t seconds, char *buffer, size_t size) {
    if (!buffer || size == 0) return;
    if (seconds == 0) {
        snprintf(buffer, size, "--");
        return;
    }
    archery_datetime_t dt;
    archery_seconds_to_datetime(seconds, &dt);
    snprintf(buffer, size, "%04u-%02u-%02u %02u:%02u", (unsigned)dt.year,
             (unsigned)dt.month, (unsigned)dt.day, (unsigned)dt.hour, (unsigned)dt.minute);
}

bool archery_clock_parse_build(const char *date, const char *time,
                               archery_datetime_t *dt) {
    if (!date || !time || !dt) return false;
    // __DATE__ 形如 "Oct 25 2026"（日可能是 " 5"），__TIME__ 形如 "07:23:54"。
    char month_text[4] = {0};
    int day = 0;
    int year = 0;
    if (sscanf(date, "%3s %d %d", month_text, &day, &year) != 3) return false;
    int month = 0;
    for (int i = 0; i < 12; i++) {
        if (strcmp(month_text, s_month_names[i]) == 0 ||
            strcmp(month_text, s_month_names_full[i]) == 0) {
            month = i + 1;
            break;
        }
    }
    if (month == 0) return false;
    int hour = 0, minute = 0, second = 0;
    if (sscanf(time, "%d:%d:%d", &hour, &minute, &second) != 3) return false;

    archery_datetime_t parsed;
    parsed.year = (uint16_t)year;
    parsed.month = (uint8_t)month;
    parsed.day = (uint8_t)day;
    parsed.hour = (uint8_t)hour;
    parsed.minute = (uint8_t)minute;
    parsed.second = (uint8_t)second;
    if (!archery_datetime_valid(&parsed)) return false;
    *dt = parsed;
    return true;
}
