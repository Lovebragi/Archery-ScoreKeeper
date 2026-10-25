// main/archery_history.c —— 记录编码与环形槽位实现。
#include "archery_history.h"

#include <string.h>

// CRC-16/CCITT-FALSE（poly 0x1021, init 0xFFFF）：只用于识别 Flash 中的
// 不完整或损坏记录，不承担安全职责。
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

static uint8_t packed_byte_at(const archery_record_t *record, uint8_t index) {
    return record->packed[index / 2u];
}

static void packed_set(archery_record_t *record, uint8_t index, uint8_t code) {
    uint8_t *byte = &record->packed[index / 2u];
    if ((index & 1u) == 0u) {
        *byte = (uint8_t)((*byte & 0x0Fu) | (uint8_t)(code << 4));
    } else {
        *byte = (uint8_t)((*byte & 0xF0u) | (code & 0x0Fu));
    }
}

static uint8_t packed_get(const archery_record_t *record, uint8_t index) {
    const uint8_t byte = packed_byte_at(record, index);
    return (index & 1u) == 0u ? (uint8_t)(byte >> 4) : (uint8_t)(byte & 0x0Fu);
}

void archery_history_meta_init(archery_history_meta_t *meta) {
    meta->count = 0;
    meta->next_seq = 1;
}

void archery_history_meta_after_save(archery_history_meta_t *meta) {
    if (meta->count < ARCHERY_HISTORY_CAPACITY) meta->count++;
    if (meta->next_seq < UINT16_MAX) meta->next_seq++;
}

uint16_t archery_history_slot_for_seq(uint16_t seq) {
    if (seq == 0) return ARCHERY_HISTORY_SLOT_INVALID;
    return (uint16_t)((seq - 1u) % ARCHERY_HISTORY_CAPACITY);
}

uint16_t archery_history_seq_newest(const archery_history_meta_t *meta,
                                    uint16_t newest_index) {
    if (meta->next_seq <= 1u) return 0;
    const uint16_t seq = (uint16_t)(meta->next_seq - 1u);
    if (newest_index >= seq) return 0;
    const uint16_t result = (uint16_t)(seq - newest_index);
    // 超过 50 次后旧记录已被覆盖。
    if (newest_index >= ARCHERY_HISTORY_CAPACITY) return 0;
    return result;
}

void archery_record_from_session(archery_record_t *record,
                                 const archery_session_t *session,
                                 uint16_t seq, uint16_t duration_s, uint32_t when) {
    memset(record, 0, sizeof(*record));
    record->seq = seq;
    record->duration_s = duration_s;
    record->when = when;
    record->groups = session->groups;
    record->arrows = session->arrows;
    record->target = session->target;
    uint8_t index = 0;
    for (uint8_t g = 0; g < session->groups; g++) {
        for (uint8_t a = 0; a < session->arrows; a++) {
            packed_set(record, index, session->scores[g][a]);
            index++;
        }
    }
}

uint8_t archery_record_score(const archery_record_t *record,
                             uint8_t group, uint8_t arrow) {
    if (group >= record->groups || arrow >= record->arrows) {
        return ARCHERY_RECORD_SCORE_INVALID;
    }
    const uint8_t index = (uint8_t)(group * record->arrows + arrow);
    if (index >= ARCHERY_RECORD_MAX_ARROWS) return ARCHERY_RECORD_SCORE_INVALID;
    return packed_get(record, index);
}

uint8_t archery_record_arrow_count(const archery_record_t *record) {
    return (uint8_t)(record->groups * record->arrows);
}

uint8_t archery_record_group_points(const archery_record_t *record, uint8_t group) {
    if (group >= record->groups) return 0;
    uint8_t points = 0;
    for (uint8_t a = 0; a < record->arrows; a++) {
        points = (uint8_t)(points + archery_score_points(archery_record_score(record, group, a)));
    }
    return points;
}

uint8_t archery_record_group_x(const archery_record_t *record, uint8_t group) {
    if (group >= record->groups) return 0;
    uint8_t count = 0;
    for (uint8_t a = 0; a < record->arrows; a++) {
        if (archery_score_is_x(archery_record_score(record, group, a))) count++;
    }
    return count;
}

uint8_t archery_record_group_miss(const archery_record_t *record, uint8_t group) {
    if (group >= record->groups) return 0;
    uint8_t count = 0;
    for (uint8_t a = 0; a < record->arrows; a++) {
        if (archery_score_is_miss(archery_record_score(record, group, a))) count++;
    }
    return count;
}

uint8_t archery_record_ring_count(const archery_record_t *record,
                                  uint8_t group, uint8_t slot) {
    const uint8_t code = archery_target_slot_code(record->target, slot);
    if (group >= record->groups || code == ARCHERY_RING_SLOT_INVALID) return 0;
    uint8_t count = 0;
    for (uint8_t a = 0; a < record->arrows; a++) {
        if (archery_record_score(record, group, a) == code) count++;
    }
    return count;
}

uint16_t archery_record_total_points(const archery_record_t *record) {
    uint16_t total = 0;
    for (uint8_t g = 0; g < record->groups; g++) {
        total = (uint16_t)(total + archery_record_group_points(record, g));
    }
    return total;
}

uint8_t archery_record_total_x(const archery_record_t *record) {
    uint8_t count = 0;
    for (uint8_t g = 0; g < record->groups; g++) {
        count = (uint8_t)(count + archery_record_group_x(record, g));
    }
    return count;
}

uint8_t archery_record_total_miss(const archery_record_t *record) {
    uint8_t count = 0;
    for (uint8_t g = 0; g < record->groups; g++) {
        count = (uint8_t)(count + archery_record_group_miss(record, g));
    }
    return count;
}

uint8_t archery_record_best_group(const archery_record_t *record) {
    uint8_t best = 0;
    for (uint8_t g = 0; g < record->groups; g++) {
        const uint8_t points = archery_record_group_points(record, g);
        if (points > best) best = points;
    }
    return best;
}

size_t archery_record_encode(const archery_record_t *record,
                             uint8_t *buffer, size_t capacity) {
    if (record->groups < ARCHERY_GROUPS_MIN || record->groups > ARCHERY_GROUPS_MAX) return 0;
    if (record->arrows < ARCHERY_ARROWS_MIN || record->arrows > ARCHERY_ARROWS_MAX) return 0;
    if (!archery_target_valid(record->target)) return 0;
    const size_t score_bytes =
        ((size_t)record->groups * record->arrows + 1u) / 2u;
    const size_t length = ARCHERY_RECORD_HEADER_BYTES + score_bytes + 2u;
    if (capacity < length) return 0;

    buffer[0] = ARCHERY_RECORD_MAGIC;
    buffer[1] = ARCHERY_RECORD_VERSION;
    buffer[2] = record->groups;
    buffer[3] = record->arrows;
    buffer[4] = record->target;
    buffer[5] = (uint8_t)(record->seq & 0xFFu);
    buffer[6] = (uint8_t)(record->seq >> 8);
    buffer[7] = (uint8_t)(record->duration_s & 0xFFu);
    buffer[8] = (uint8_t)(record->duration_s >> 8);
    buffer[9] = (uint8_t)(record->when & 0xFFu);
    buffer[10] = (uint8_t)((record->when >> 8) & 0xFFu);
    buffer[11] = (uint8_t)((record->when >> 16) & 0xFFu);
    buffer[12] = (uint8_t)((record->when >> 24) & 0xFFu);
    // 只有前 groups*arrows 个环值有意义；校验每个都必须是合法环值。
    for (size_t i = 0; i < (size_t)record->groups * record->arrows; i++) {
        const uint8_t code = packed_get(record, (uint8_t)i);
        if (!archery_score_valid(code)) return 0;
    }
    memcpy(&buffer[ARCHERY_RECORD_HEADER_BYTES], record->packed, score_bytes);
    const uint16_t crc = crc16_ccitt(buffer, length - 2u);
    buffer[length - 2u] = (uint8_t)(crc & 0xFFu);
    buffer[length - 1u] = (uint8_t)(crc >> 8);
    return length;
}

bool archery_record_decode(archery_record_t *record,
                           const uint8_t *buffer, size_t length) {
    if (length < 10u) return false;
    if (buffer[0] != ARCHERY_RECORD_MAGIC) return false;

    const uint8_t version = buffer[1];
    size_t header_bytes;
    if (version == ARCHERY_RECORD_VERSION) {
        header_bytes = ARCHERY_RECORD_HEADER_BYTES;
    } else if (version == ARCHERY_RECORD_VERSION_V2) {
        header_bytes = ARCHERY_RECORD_HEADER_BYTES - 4u;  // 旧版没有时间戳
    } else if (version == ARCHERY_RECORD_VERSION_V1) {
        header_bytes = ARCHERY_RECORD_HEADER_BYTES - 5u;  // 旧版没有靶型与时间戳
    } else {
        return false;
    }

    const uint8_t groups = buffer[2];
    const uint8_t arrows = buffer[3];
    if (groups < ARCHERY_GROUPS_MIN || groups > ARCHERY_GROUPS_MAX) return false;
    if (arrows < ARCHERY_ARROWS_MIN || arrows > ARCHERY_ARROWS_MAX) return false;
    const size_t score_bytes = ((size_t)groups * arrows + 1u) / 2u;
    if (length != header_bytes + score_bytes + 2u) return false;
    const uint16_t expected =
        (uint16_t)(buffer[length - 2u] | ((uint16_t)buffer[length - 1u] << 8));
    if (crc16_ccitt(buffer, length - 2u) != expected) return false;

    archery_record_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    decoded.groups = groups;
    decoded.arrows = arrows;
    size_t seq_offset = 4;
    if (version == ARCHERY_RECORD_VERSION || version == ARCHERY_RECORD_VERSION_V2) {
        decoded.target = buffer[4];
        if (!archery_target_valid(decoded.target)) return false;
        seq_offset = 5;
    } else {
        decoded.target = ARCHERY_TARGET_HALF_40;  // 旧记录按 40 半靶解释
    }
    decoded.seq = (uint16_t)(buffer[seq_offset] | ((uint16_t)buffer[seq_offset + 1] << 8));
    decoded.duration_s = (uint16_t)(buffer[seq_offset + 2] |
                                    ((uint16_t)buffer[seq_offset + 3] << 8));
    if (version == ARCHERY_RECORD_VERSION) {
        decoded.when = (uint32_t)buffer[9] | ((uint32_t)buffer[10] << 8) |
                       ((uint32_t)buffer[11] << 16) | ((uint32_t)buffer[12] << 24);
    }
    memcpy(decoded.packed, &buffer[header_bytes], score_bytes);
    for (size_t i = 0; i < (size_t)groups * arrows; i++) {
        if (!archery_score_valid(packed_get(&decoded, (uint8_t)i))) return false;
    }
    *record = decoded;
    return true;
}
