// main/archery_settings.c —— 靶型/屏幕方向/亮度/息屏时间的选项表与编解码。
#include "archery_settings.h"

#include <string.h>

#define SETTINGS_MAGIC 0x5Au
#define SETTINGS_VERSION 3u
#define SETTINGS_VERSION_V2 2u
#define SETTINGS_VERSION_V1 1u
#define SETTINGS_CRC_BYTES_V3 8u
#define SETTINGS_CRC_BYTES_V2 7u
#define SETTINGS_CRC_BYTES_V1 6u

static const uint8_t s_targets[] = {
    ARCHERY_TARGET_HALF_40,
    ARCHERY_TARGET_TRIPLE_40H,
    ARCHERY_TARGET_FULL_40,
    ARCHERY_TARGET_FULL_80,
    ARCHERY_TARGET_FULL_122,
};
static const uint8_t s_brightness[] = {20, 40, 60, 80, 100};
static const uint16_t s_off_seconds[] = {0, 15, 30, 60, 120};

#define TARGET_COUNT ((uint8_t)(sizeof(s_targets) / sizeof(s_targets[0])))
#define BRIGHTNESS_COUNT ((uint8_t)(sizeof(s_brightness) / sizeof(s_brightness[0])))
#define OFF_COUNT ((uint8_t)(sizeof(s_off_seconds) / sizeof(s_off_seconds[0])))

static uint16_t crc16_ccitt(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < length; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

void archery_settings_defaults(archery_settings_t *settings) {
    settings->target = ARCHERY_SETTINGS_DEFAULT_TARGET;
    settings->rotate = ARCHERY_SETTINGS_DEFAULT_ROTATE;
    settings->brightness = ARCHERY_SETTINGS_DEFAULT_BRIGHTNESS;
    settings->off_seconds = ARCHERY_SETTINGS_DEFAULT_OFF_SECONDS;
}

uint8_t archery_settings_target_count(void) { return TARGET_COUNT; }
uint8_t archery_settings_target_at(uint8_t index) {
    return index < TARGET_COUNT ? s_targets[index] : s_targets[0];
}
uint8_t archery_settings_brightness_count(void) { return BRIGHTNESS_COUNT; }
uint8_t archery_settings_brightness_at(uint8_t index) {
    return index < BRIGHTNESS_COUNT ? s_brightness[index] : s_brightness[0];
}
uint8_t archery_settings_off_count(void) { return OFF_COUNT; }
uint16_t archery_settings_off_at(uint8_t index) {
    return index < OFF_COUNT ? s_off_seconds[index] : s_off_seconds[0];
}

static int target_index(uint8_t value) {
    for (uint8_t i = 0; i < TARGET_COUNT; i++) {
        if (s_targets[i] == value) return i;
    }
    return -1;
}

static int brightness_index(uint8_t value) {
    for (uint8_t i = 0; i < BRIGHTNESS_COUNT; i++) {
        if (s_brightness[i] == value) return i;
    }
    return -1;
}

static int off_index(uint16_t value) {
    for (uint8_t i = 0; i < OFF_COUNT; i++) {
        if (s_off_seconds[i] == value) return i;
    }
    return -1;
}

bool archery_settings_valid(const archery_settings_t *settings) {
    if (!settings) return false;
    return archery_target_valid(settings->target) &&
           target_index(settings->target) >= 0 &&
           settings->rotate <= 1u &&
           brightness_index(settings->brightness) >= 0 &&
           off_index(settings->off_seconds) >= 0;
}

bool archery_settings_adjust(archery_settings_t *settings, uint8_t row, int delta) {
    if (!settings) return false;
    if (row == 0) {
        int index = target_index(settings->target);
        if (index < 0) index = 0;
        const int next = index + delta;
        if (next < 0 || next >= (int)TARGET_COUNT || next == index) return false;
        settings->target = s_targets[next];
        return true;
    }
    if (row == 1) {
        // 只有两个方向，任一方向调整都相当于切换。
        const uint8_t next = settings->rotate ? 0u : 1u;
        if (next == settings->rotate) return false;
        settings->rotate = next;
        return true;
    }
    if (row == 2) {
        int index = brightness_index(settings->brightness);
        if (index < 0) index = 0;
        const int next = index + delta;
        if (next < 0 || next >= (int)BRIGHTNESS_COUNT || next == index) return false;
        settings->brightness = s_brightness[next];
        return true;
    }
    if (row == 3) {
        int index = off_index(settings->off_seconds);
        if (index < 0) index = 0;
        const int next = index + delta;
        if (next < 0 || next >= (int)OFF_COUNT || next == index) return false;
        settings->off_seconds = s_off_seconds[next];
        return true;
    }
    return false;
}

size_t archery_settings_encode(const archery_settings_t *settings,
                               uint8_t *buffer, size_t capacity) {
    if (!settings || !buffer || capacity < ARCHERY_SETTINGS_ENCODED_BYTES) return 0;
    if (!archery_settings_valid(settings)) return 0;
    buffer[0] = SETTINGS_MAGIC;
    buffer[1] = SETTINGS_VERSION;
    buffer[2] = settings->target;
    buffer[3] = settings->rotate;
    buffer[4] = settings->brightness;
    buffer[5] = (uint8_t)(settings->off_seconds & 0xFFu);
    buffer[6] = (uint8_t)(settings->off_seconds >> 8);
    buffer[7] = 0;  // 保留
    const uint16_t crc = crc16_ccitt(buffer, SETTINGS_CRC_BYTES_V3);
    buffer[8] = (uint8_t)(crc & 0xFFu);
    buffer[9] = (uint8_t)(crc >> 8);
    return ARCHERY_SETTINGS_ENCODED_BYTES;
}

// v1：magic, version=1, brightness, off_lo, off_hi, reserved, crc(2)
static bool decode_v1(archery_settings_t *settings, const uint8_t *buffer) {
    const uint16_t expected = (uint16_t)(buffer[6] | ((uint16_t)buffer[7] << 8));
    if (crc16_ccitt(buffer, SETTINGS_CRC_BYTES_V1) != expected) return false;
    archery_settings_t decoded;
    decoded.target = ARCHERY_TARGET_HALF_40;
    decoded.rotate = ARCHERY_SETTINGS_DEFAULT_ROTATE;
    decoded.brightness = buffer[2];
    decoded.off_seconds = (uint16_t)(buffer[3] | ((uint16_t)buffer[4] << 8));
    if (!archery_settings_valid(&decoded)) return false;
    *settings = decoded;
    return true;
}

// v2：magic, version=2, target, brightness, off_lo, off_hi, reserved, crc(2)
static bool decode_v2(archery_settings_t *settings, const uint8_t *buffer) {
    const uint16_t expected = (uint16_t)(buffer[7] | ((uint16_t)buffer[8] << 8));
    if (crc16_ccitt(buffer, SETTINGS_CRC_BYTES_V2) != expected) return false;
    archery_settings_t decoded;
    decoded.target = buffer[2];
    decoded.rotate = ARCHERY_SETTINGS_DEFAULT_ROTATE;
    decoded.brightness = buffer[3];
    decoded.off_seconds = (uint16_t)(buffer[4] | ((uint16_t)buffer[5] << 8));
    if (!archery_settings_valid(&decoded)) return false;
    *settings = decoded;
    return true;
}

bool archery_settings_decode(archery_settings_t *settings,
                             const uint8_t *buffer, size_t length) {
    if (!settings || !buffer) return false;
    if (length < ARCHERY_SETTINGS_ENCODED_BYTES_V1) return false;
    if (buffer[0] != SETTINGS_MAGIC) return false;

    switch (buffer[1]) {
    case SETTINGS_VERSION_V1:
        return length == ARCHERY_SETTINGS_ENCODED_BYTES_V1 && decode_v1(settings, buffer);
    case SETTINGS_VERSION_V2:
        return length == ARCHERY_SETTINGS_ENCODED_BYTES_V2 && decode_v2(settings, buffer);
    case SETTINGS_VERSION:
        break;
    default:
        return false;
    }
    if (length != ARCHERY_SETTINGS_ENCODED_BYTES) return false;
    const uint16_t expected = (uint16_t)(buffer[8] | ((uint16_t)buffer[9] << 8));
    if (crc16_ccitt(buffer, SETTINGS_CRC_BYTES_V3) != expected) return false;

    archery_settings_t decoded;
    decoded.target = buffer[2];
    decoded.rotate = buffer[3];
    decoded.brightness = buffer[4];
    decoded.off_seconds = (uint16_t)(buffer[5] | ((uint16_t)buffer[6] << 8));
    if (!archery_settings_valid(&decoded)) return false;
    *settings = decoded;
    return true;
}
