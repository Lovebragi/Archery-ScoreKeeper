// main/archery_score.c —— 靶型槽位表与环值换算，纯逻辑、无外部依赖。
#include "archery_score.h"

// 半靶与三联靶：靶面只有 6~10 环（10 环内含 X），脱靶记 0。
static const uint8_t s_half_slots[] = {
    ARCHERY_SCORE_X, 10, 9, 8, 7, 6, ARCHERY_SCORE_MISS,
};
// 全靶：X + 10~0，0 环就是脱靶。
static const uint8_t s_full_slots[] = {
    ARCHERY_SCORE_X, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, ARCHERY_SCORE_MISS,
};

bool archery_target_valid(uint8_t target) {
    return target < ARCHERY_TARGET_COUNT;
}

uint8_t archery_target_slot_count(uint8_t target) {
    switch (target) {
    case ARCHERY_TARGET_HALF_40:
    case ARCHERY_TARGET_TRIPLE_40H:
        return (uint8_t)(sizeof(s_half_slots) / sizeof(s_half_slots[0]));
    case ARCHERY_TARGET_FULL_40:
    case ARCHERY_TARGET_FULL_80:
    case ARCHERY_TARGET_FULL_122:
        return (uint8_t)(sizeof(s_full_slots) / sizeof(s_full_slots[0]));
    default:
        return 0;
    }
}

uint8_t archery_target_slot_code(uint8_t target, uint8_t slot) {
    switch (target) {
    case ARCHERY_TARGET_HALF_40:
    case ARCHERY_TARGET_TRIPLE_40H:
        if (slot >= sizeof(s_half_slots)) return ARCHERY_RING_SLOT_INVALID;
        return s_half_slots[slot];
    case ARCHERY_TARGET_FULL_40:
    case ARCHERY_TARGET_FULL_80:
    case ARCHERY_TARGET_FULL_122:
        if (slot >= sizeof(s_full_slots)) return ARCHERY_RING_SLOT_INVALID;
        return s_full_slots[slot];
    default:
        return ARCHERY_RING_SLOT_INVALID;
    }
}

uint8_t archery_target_entry_first_slot(uint8_t target, bool record_x) {
    if (record_x || !archery_target_valid(target)) return 0;
    // 所有靶型的槽位 0 都是 X；不记录内十时把这一行从录入页隐藏。
    return archery_target_slot_code(target, 0) == ARCHERY_SCORE_X ? 1u : 0u;
}

bool archery_score_valid(uint8_t code) {
    return code <= ARCHERY_SCORE_RING_MAX || code == ARCHERY_SCORE_X;
}

uint8_t archery_score_points(uint8_t code) {
    if (code == ARCHERY_SCORE_X) return ARCHERY_SCORE_RING_MAX;
    return code <= ARCHERY_SCORE_RING_MAX ? code : 0;
}

const char *archery_score_text(uint8_t code) {
    switch (code) {
    case ARCHERY_SCORE_MISS: return "M";
    case ARCHERY_SCORE_X: return "X";
    case 1: return "1";
    case 2: return "2";
    case 3: return "3";
    case 4: return "4";
    case 5: return "5";
    case 6: return "6";
    case 7: return "7";
    case 8: return "8";
    case 9: return "9";
    case 10: return "10";
    default: return "?";
    }
}

bool archery_score_is_x(uint8_t code) { return code == ARCHERY_SCORE_X; }
bool archery_score_is_miss(uint8_t code) { return code == ARCHERY_SCORE_MISS; }
