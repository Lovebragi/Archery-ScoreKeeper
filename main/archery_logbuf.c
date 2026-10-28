// main/archery_logbuf.c —— 拦截 ESP_LOG 输出，保留最近若干行供诊断页查看。
#include "archery_logbuf.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static char s_lines[ARCHERY_LOG_LINES][ARCHERY_LOG_LINE_CHARS];
static size_t s_head;
static size_t s_count;
static SemaphoreHandle_t s_lock;
static vprintf_like_t s_previous;

static int log_sink(const char *format, va_list args) {
    char line[ARCHERY_LOG_LINE_CHARS];
    va_list copy;
    va_copy(copy, args);
    vsnprintf(line, sizeof(line), format, copy);
    va_end(copy);

    size_t length = strlen(line);
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        line[--length] = '\0';
    }
    if (length > 0 && s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        size_t keep = strlen(line);
        if (keep > ARCHERY_LOG_LINE_CHARS - 1u) keep = ARCHERY_LOG_LINE_CHARS - 1u;
        while (keep > 0 && ((unsigned char)line[keep] & 0xC0u) == 0x80u) keep--;
        memcpy(s_lines[s_head], line, keep);
        s_lines[s_head][keep] = '\0';
        s_head = (s_head + 1u) % ARCHERY_LOG_LINES;
        if (s_count < ARCHERY_LOG_LINES) s_count++;
        xSemaphoreGive(s_lock);
    }

    return s_previous ? s_previous(format, args) : vprintf(format, args);
}

void archery_logbuf_init(void) {
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock || s_previous) return;
    s_previous = esp_log_set_vprintf(log_sink);
}

size_t archery_logbuf_count(void) {
    s_count = s_count > ARCHERY_LOG_LINES ? ARCHERY_LOG_LINES : s_count;
    return s_count;
}

void archery_logbuf_snapshot(char *out, size_t capacity) {
    if (!out || capacity == 0) return;
    out[0] = '\0';
    if (!s_lock || s_count == 0) return;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    const size_t count = s_count;
    const size_t start = (s_head + ARCHERY_LOG_LINES - count) % ARCHERY_LOG_LINES;
    size_t used = 0;
    for (size_t i = 0; i < count && used + 1 < capacity; i++) {
        const char *line = s_lines[(start + i) % ARCHERY_LOG_LINES];
        const int written = snprintf(out + used, capacity - used, "%s%s", line,
                                     i + 1 < count ? "\n" : "");
        if (written <= 0) break;
        used += (size_t)written;
    }
    out[capacity - 1] = '\0';
    xSemaphoreGive(s_lock);
}
