// tests/test_archery_record.c —— 历史记录编码与槽位计算测试。
#include "archery_history.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void fill_session(archery_session_t *session, uint8_t groups, uint8_t arrows) {
    static const uint8_t allowed[] = {0, 6, 7, 8, 9, 10, 11};
    archery_session_begin(session, groups, arrows, ARCHERY_TARGET_HALF_40);
    size_t index = 0;
    for (uint8_t g = 0; g < session->groups; g++) {
        for (uint8_t a = 0; a < session->arrows; a++) {
            session->scores[g][a] = allowed[index % (sizeof(allowed) / sizeof(allowed[0]))];
            index++;
        }
    }
}

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

static void test_round_trip(void) {
    static const uint8_t groups_list[] = {1, 2, 6, 12};
    static const uint8_t arrows_list[] = {1, 3, 6, 12};
    for (size_t i = 0; i < sizeof(groups_list); i++) {
        archery_session_t session;
        fill_session(&session, groups_list[i], arrows_list[i]);
        archery_record_t record;
        archery_record_from_session(&record, &session, 7, 125, 0x5A5A1234u);
        assert(record.seq == 7);
        assert(record.duration_s == 125);

        uint8_t buffer[ARCHERY_RECORD_BUFFER_BYTES];
        const size_t length = archery_record_encode(&record, buffer, sizeof(buffer));
        assert(length > 0 && length <= sizeof(buffer));

        archery_record_t decoded;
        memset(&decoded, 0xAA, sizeof(decoded));
        assert(archery_record_decode(&decoded, buffer, length));
        assert(decoded.seq == record.seq);
        assert(decoded.duration_s == record.duration_s);
        assert(decoded.groups == record.groups);
        assert(decoded.target == record.target);
        assert(decoded.when == record.when);
        assert(decoded.arrows == record.arrows);
        assert(archery_record_total_points(&decoded) ==
               archery_record_total_points(&record));
        for (uint8_t g = 0; g < decoded.groups; g++) {
            for (uint8_t a = 0; a < decoded.arrows; a++) {
                assert(archery_record_score(&decoded, g, a) ==
                       archery_record_score(&record, g, a));
            }
        }
    }
}

static void test_totals_match_session(void) {
    archery_session_t session;
    fill_session(&session, 3, 4);
    archery_record_t record;
    archery_record_from_session(&record, &session, 1, 60, 0);

    assert(archery_record_total_points(&record) ==
           archery_session_total_points(&session));
    assert(archery_record_total_x(&record) == archery_session_total_x(&session));
    assert(archery_record_total_miss(&record) == archery_session_total_miss(&session));
    assert(archery_record_best_group(&record) == archery_session_best_group(&session));
    assert(archery_record_arrow_count(&record) == 12);
    // 各环箭数之和等于每组箭数，且与源会话一致
    for (uint8_t g = 0; g < record.groups; g++) {
        uint8_t sum = 0;
        for (uint8_t slot = 0; slot < archery_target_slot_count(record.target); slot++) {
            const uint8_t count = archery_record_ring_count(&record, g, slot);
            assert(count == archery_session_ring_count(&session, g, slot));
            sum = (uint8_t)(sum + count);
        }
        assert(sum == record.arrows);
    }
    assert(archery_record_ring_count(&record, 4, 0) == 0);
    // 越界访问返回哨兵值，而不是读到相邻数据
    assert(archery_record_score(&record, 3, 0) == ARCHERY_RECORD_SCORE_INVALID);
    assert(archery_record_score(&record, 0, 4) == ARCHERY_RECORD_SCORE_INVALID);
    assert(archery_record_group_points(&record, 3) == 0);
}

static void expect_decode_failure(const uint8_t *buffer, size_t length) {
    archery_record_t decoded;
    assert(!archery_record_decode(&decoded, buffer, length));
}

static void test_corruption_detected(void) {
    archery_session_t session;
    fill_session(&session, 2, 3);
    archery_record_t record;
    archery_record_from_session(&record, &session, 2, 30, 1234567u);

    uint8_t buffer[ARCHERY_RECORD_BUFFER_BYTES];
    const size_t length = archery_record_encode(&record, buffer, sizeof(buffer));
    assert(length > 0);

    uint8_t copy[ARCHERY_RECORD_BUFFER_BYTES];

    memcpy(copy, buffer, length);
    copy[0] ^= 0xFF;  // magic
    expect_decode_failure(copy, length);

    memcpy(copy, buffer, length);
    copy[1] = 0x7F;   // 版本
    expect_decode_failure(copy, length);

    memcpy(copy, buffer, length);
    copy[2] = 0;      // 组数非法
    expect_decode_failure(copy, length);

    memcpy(copy, buffer, length);
    copy[3] = 13;     // 箭数非法
    expect_decode_failure(copy, length);

    memcpy(copy, buffer, length);
    copy[4] = 99;     // 靶型非法
    expect_decode_failure(copy, length);

    memcpy(copy, buffer, length);
    copy[length - 1] ^= 0x01;  // CRC
    expect_decode_failure(copy, length);

    expect_decode_failure(buffer, length - 1);
    expect_decode_failure(buffer, 0);

    // 篡改一个环值为非法编码并重算 CRC：解码仍应拒绝坏环值。
    memcpy(copy, buffer, length);
    copy[ARCHERY_RECORD_HEADER_BYTES] = 0xCC;  // 高半字节 12 不是合法环值
    const uint16_t crc = crc16_ccitt(copy, length - 2);
    copy[length - 2] = (uint8_t)(crc & 0xFF);
    copy[length - 1] = (uint8_t)(crc >> 8);
    expect_decode_failure(copy, length);

    // 编码容量不足
    assert(archery_record_encode(&record, copy, 4) == 0);
}

// 旧版记录没有靶型字段：解码应把它当作 40 半靶，页码/序号/用时保持不变。
static void test_v1_record_compatibility(void) {
    archery_session_t session;
    fill_session(&session, 1, 2);
    archery_record_t record;
    archery_record_from_session(&record, &session, 5, 60, 0);

    uint8_t old[ARCHERY_RECORD_BUFFER_BYTES];
    const size_t score_bytes = 1;  // (1*2+1)/2
    const size_t length = 8u + score_bytes + 2u;
    old[0] = ARCHERY_RECORD_MAGIC;
    old[1] = ARCHERY_RECORD_VERSION_V1;
    old[2] = 1;   // groups
    old[3] = 2;   // arrows
    old[4] = (uint8_t)(record.seq & 0xFFu);
    old[5] = (uint8_t)(record.seq >> 8);
    old[6] = (uint8_t)(record.duration_s & 0xFFu);
    old[7] = (uint8_t)(record.duration_s >> 8);
    old[8] = record.packed[0];
    const uint16_t crc = crc16_ccitt(old, length - 2u);
    old[length - 2u] = (uint8_t)(crc & 0xFFu);
    old[length - 1u] = (uint8_t)(crc >> 8);

    archery_record_t decoded;
    assert(archery_record_decode(&decoded, old, length));
    assert(decoded.target == ARCHERY_TARGET_HALF_40);
    assert(decoded.seq == 5);
    assert(decoded.duration_s == 60);
    assert(decoded.arrows == 2);
    assert(archery_record_total_points(&decoded) == archery_record_total_points(&record));
}

static void test_history_meta_and_slots(void) {
    archery_history_meta_t meta;
    archery_history_meta_init(&meta);
    assert(meta.count == 0 && meta.next_seq == 1);
    assert(archery_history_seq_newest(&meta, 0) == 0);
    assert(archery_history_slot_for_seq(0) == ARCHERY_HISTORY_SLOT_INVALID);

    assert(archery_history_slot_for_seq(1) == 0);
    assert(archery_history_slot_for_seq(300) == 299);
    assert(archery_history_slot_for_seq(301) == 0);  // 回绕覆盖最旧记录

    for (int i = 0; i < 300; i++) archery_history_meta_after_save(&meta);
    assert(meta.count == 300);
    assert(meta.next_seq == 301);
    // 第 0 新 = seq 300，槽位 299；第 299 新 = seq 1，槽位 0
    assert(archery_history_seq_newest(&meta, 0) == 300);
    assert(archery_history_seq_newest(&meta, 299) == 1);
    assert(archery_history_seq_newest(&meta, 300) == 0);

    // 超过容量后只保留最近 300 条，序号继续增长
    for (int i = 0; i < 10; i++) archery_history_meta_after_save(&meta);
    assert(meta.count == 300);
    assert(meta.next_seq == 311);
    assert(archery_history_seq_newest(&meta, 0) == 310);
    assert(archery_history_seq_newest(&meta, 299) == 11);
    assert(archery_history_seq_newest(&meta, 300) == 0);
}

int main(void) {
    test_round_trip();
    test_totals_match_session();
    test_corruption_detected();
    test_v1_record_compatibility();
    test_history_meta_and_slots();
    puts("archery history record/slot tests: PASS");
    return 0;
}
