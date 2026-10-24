// main/archery_session.c —— 训练会话的纯逻辑实现。
#include "archery_session.h"

#include <string.h>

uint8_t archery_groups_clamp(int value) {
    if (value < (int)ARCHERY_GROUPS_MIN) return ARCHERY_GROUPS_MIN;
    if (value > (int)ARCHERY_GROUPS_MAX) return ARCHERY_GROUPS_MAX;
    return (uint8_t)value;
}

uint8_t archery_arrows_clamp(int value) {
    if (value < (int)ARCHERY_ARROWS_MIN) return ARCHERY_ARROWS_MIN;
    if (value > (int)ARCHERY_ARROWS_MAX) return ARCHERY_ARROWS_MAX;
    return (uint8_t)value;
}

void archery_session_begin(archery_session_t *session, int groups, int arrows,
                           uint8_t target) {
    memset(session, 0, sizeof(*session));
    session->groups = archery_groups_clamp(groups);
    session->arrows = archery_arrows_clamp(arrows);
    session->target = archery_target_valid(target) ? target : ARCHERY_TARGET_HALF_40;
    session->group = 0;
    // 所有箭先记 0 环：累计环数从 0 开始，等各组统计提交时再展开。
}

bool archery_session_complete(const archery_session_t *session) {
    return session->group >= session->groups;
}

static bool group_in_range(const archery_session_t *session, uint8_t group) {
    return group < session->groups;
}

uint8_t archery_session_group_points(const archery_session_t *session, uint8_t group) {
    if (!group_in_range(session, group)) return 0;
    uint8_t points = 0;
    for (uint8_t a = 0; a < session->arrows; a++) {
        points = (uint8_t)(points + archery_score_points(session->scores[group][a]));
    }
    return points;
}

uint8_t archery_session_group_x(const archery_session_t *session, uint8_t group) {
    if (!group_in_range(session, group)) return 0;
    uint8_t count = 0;
    for (uint8_t a = 0; a < session->arrows; a++) {
        if (archery_score_is_x(session->scores[group][a])) count++;
    }
    return count;
}

uint8_t archery_session_group_miss(const archery_session_t *session, uint8_t group) {
    if (!group_in_range(session, group)) return 0;
    uint8_t count = 0;
    for (uint8_t a = 0; a < session->arrows; a++) {
        if (archery_score_is_miss(session->scores[group][a])) count++;
    }
    return count;
}

uint8_t archery_session_ring_count(const archery_session_t *session,
                                   uint8_t group, uint8_t slot) {
    const uint8_t code = archery_target_slot_code(session->target, slot);
    if (!group_in_range(session, group) || code == ARCHERY_RING_SLOT_INVALID) return 0;
    uint8_t count = 0;
    for (uint8_t a = 0; a < session->arrows; a++) {
        if (session->scores[group][a] == code) count++;
    }
    return count;
}

uint16_t archery_session_total_points(const archery_session_t *session) {
    uint16_t total = 0;
    for (uint8_t g = 0; g < session->groups; g++) {
        total = (uint16_t)(total + archery_session_group_points(session, g));
    }
    return total;
}

uint8_t archery_session_total_x(const archery_session_t *session) {
    uint8_t count = 0;
    for (uint8_t g = 0; g < session->groups; g++) {
        count = (uint8_t)(count + archery_session_group_x(session, g));
    }
    return count;
}

uint8_t archery_session_total_miss(const archery_session_t *session) {
    uint8_t count = 0;
    for (uint8_t g = 0; g < session->groups; g++) {
        count = (uint8_t)(count + archery_session_group_miss(session, g));
    }
    return count;
}

uint8_t archery_session_arrow_count(const archery_session_t *session) {
    return (uint8_t)(session->groups * session->arrows);
}

uint16_t archery_session_max_points(const archery_session_t *session) {
    return (uint16_t)(ARCHERY_SCORE_RING_MAX * archery_session_arrow_count(session));
}

uint16_t archery_session_average_x10(const archery_session_t *session) {
    const uint16_t arrows = archery_session_arrow_count(session);
    if (arrows == 0) return 0;
    const uint32_t scaled = (uint32_t)archery_session_total_points(session) * 10u;
    return (uint16_t)((scaled + arrows / 2u) / arrows);
}

uint8_t archery_session_best_group(const archery_session_t *session) {
    uint8_t best = 0;
    for (uint8_t g = 0; g < session->groups; g++) {
        const uint8_t points = archery_session_group_points(session, g);
        if (points > best) best = points;
    }
    return best;
}

uint8_t archery_counts_total(const uint8_t *counts) {
    uint8_t total = 0;
    for (uint8_t slot = 0; slot < ARCHERY_RING_SLOT_MAX; slot++) {
        total = (uint8_t)(total + counts[slot]);
    }
    return total;
}

uint16_t archery_counts_points(const uint8_t *counts, uint8_t target) {
    uint16_t points = 0;
    const uint8_t slots = archery_target_slot_count(target);
    for (uint8_t slot = 0; slot < slots; slot++) {
        const uint8_t code = archery_target_slot_code(target, slot);
        points = (uint16_t)(points + counts[slot] * archery_score_points(code));
    }
    return points;
}

bool archery_counts_adjust(uint8_t *counts, uint8_t slot, int delta, uint8_t arrows,
                           uint8_t target) {
    if (slot >= archery_target_slot_count(target)) return false;
    const int current = counts[slot];
    int next = current + delta;
    if (next < 0) next = 0;
    if (next > (int)arrows) next = (int)arrows;
    if (next == current) return false;
    if (next > current) {
        // 增加后总数不得超过每组箭数（delta 可大于 1，因此按剩余额度截断）。
        const uint8_t total = archery_counts_total(counts);
        const uint8_t remaining = total < arrows ? (uint8_t)(arrows - total) : 0;
        if (remaining == 0) return false;
        if (next - current > (int)remaining) next = current + (int)remaining;
    }
    counts[slot] = (uint8_t)next;
    return true;
}

bool archery_session_apply_face_counts(archery_session_t *session,
                                       uint8_t arrow_offset, uint8_t face_arrows,
                                       const uint8_t *counts) {
    if (archery_session_complete(session)) return false;
    if ((uint16_t)arrow_offset + face_arrows > session->arrows) return false;
    if (archery_counts_total(counts) != face_arrows) return false;
    const uint8_t slots = archery_target_slot_count(session->target);
    if (slots == 0) return false;

    uint8_t index = arrow_offset;
    for (uint8_t slot = 0; slot < slots; slot++) {
        const uint8_t code = archery_target_slot_code(session->target, slot);
        for (uint8_t i = 0; i < counts[slot]; i++) {
            if (index >= (uint8_t)(arrow_offset + face_arrows)) return false;
            session->scores[session->group][index] = code;
            index++;
        }
    }
    return index == (uint8_t)(arrow_offset + face_arrows);
}

bool archery_session_apply_group_counts(archery_session_t *session,
                                        const uint8_t *counts) {
    return archery_session_apply_face_counts(session, 0, session->arrows, counts);
}

bool archery_session_commit_group(archery_session_t *session) {
    if (archery_session_complete(session)) return false;
    if ((uint8_t)(session->group + 1u) < session->groups) {
        session->group++;
        return true;
    }
    // 最后一组提交后 group == groups，作为"已完成"的哨兵值。
    session->group = session->groups;
    return false;
}
