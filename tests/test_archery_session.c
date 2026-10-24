// tests/test_archery_session.c —— 靶型、环值、各环箭数与训练会话的纯逻辑测试。
#include "archery_score.h"
#include "archery_session.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_target_tables(void) {
    assert(archery_target_valid(ARCHERY_TARGET_HALF_40));
    assert(archery_target_valid(ARCHERY_TARGET_FULL_122));
    assert(!archery_target_valid(ARCHERY_TARGET_COUNT));
    assert(!archery_target_valid(200));

    // 半靶/三联靶：X, 10..6, 0 共 7 槽
    assert(archery_target_slot_count(ARCHERY_TARGET_HALF_40) == 7);
    assert(archery_target_slot_count(ARCHERY_TARGET_TRIPLE_40H) == 7);
    assert(archery_target_slot_code(ARCHERY_TARGET_HALF_40, 0) == ARCHERY_SCORE_X);
    assert(archery_target_slot_code(ARCHERY_TARGET_HALF_40, 1) == 10);
    assert(archery_target_slot_code(ARCHERY_TARGET_HALF_40, 5) == 6);
    assert(archery_target_slot_code(ARCHERY_TARGET_HALF_40, 6) == ARCHERY_SCORE_MISS);
    assert(archery_target_slot_code(ARCHERY_TARGET_HALF_40, 7) ==
           ARCHERY_RING_SLOT_INVALID);

    // 全靶：X, 10..1, 0 共 12 槽
    assert(archery_target_slot_count(ARCHERY_TARGET_FULL_40) == 12);
    assert(archery_target_slot_count(ARCHERY_TARGET_FULL_80) == 12);
    assert(archery_target_slot_count(ARCHERY_TARGET_FULL_122) == 12);
    assert(archery_target_slot_code(ARCHERY_TARGET_FULL_40, 0) == ARCHERY_SCORE_X);
    assert(archery_target_slot_code(ARCHERY_TARGET_FULL_40, 1) == 10);
    assert(archery_target_slot_code(ARCHERY_TARGET_FULL_40, 10) == 1);
    assert(archery_target_slot_code(ARCHERY_TARGET_FULL_40, 11) == ARCHERY_SCORE_MISS);
    assert(archery_target_slot_code(ARCHERY_TARGET_FULL_40, 12) ==
           ARCHERY_RING_SLOT_INVALID);
    // 非法靶型没有槽位
    assert(archery_target_slot_count(ARCHERY_TARGET_COUNT) == 0);
}

static void test_score_table(void) {
    for (uint8_t code = 0; code <= 10; code++) assert(archery_score_valid(code));
    assert(archery_score_valid(ARCHERY_SCORE_X));
    assert(!archery_score_valid(12));
    assert(!archery_score_valid(0xFF));

    assert(archery_score_points(ARCHERY_SCORE_MISS) == 0);
    assert(archery_score_points(1) == 1);
    assert(archery_score_points(6) == 6);
    assert(archery_score_points(10) == 10);
    assert(archery_score_points(ARCHERY_SCORE_X) == 10);

    assert(archery_score_is_miss(0));
    assert(archery_score_is_x(ARCHERY_SCORE_X));
    assert(!archery_score_is_x(10));

    assert(archery_score_text(0)[0] == 'M');
    assert(archery_score_text(ARCHERY_SCORE_X)[0] == 'X');
    assert(archery_score_text(1)[0] == '1');
    assert(archery_score_text(10)[0] == '1');
    assert(archery_score_text(10)[1] == '0');
}

static void test_counts_helpers(void) {
    uint8_t counts[ARCHERY_RING_SLOT_MAX] = {0};
    assert(archery_counts_total(counts) == 0);
    assert(archery_counts_points(counts, ARCHERY_TARGET_HALF_40) == 0);

    // 半靶 3 根箭：X 1 根、10 1 根、9 1 根 -> 29 环
    assert(archery_counts_adjust(counts, 0, 1, 3, ARCHERY_TARGET_HALF_40));
    assert(archery_counts_adjust(counts, 1, 1, 3, ARCHERY_TARGET_HALF_40));
    assert(archery_counts_adjust(counts, 2, 1, 3, ARCHERY_TARGET_HALF_40));
    assert(archery_counts_total(counts) == 3);
    assert(archery_counts_points(counts, ARCHERY_TARGET_HALF_40) == 10 + 10 + 9);

    // 已经记满时不允许继续增加
    assert(!archery_counts_adjust(counts, 3, 1, 3, ARCHERY_TARGET_HALF_40));
    // 可以减少，且不能减到负数
    assert(archery_counts_adjust(counts, 2, -1, 3, ARCHERY_TARGET_HALF_40));
    assert(archery_counts_total(counts) == 2);
    assert(!archery_counts_adjust(counts, 2, -1, 3, ARCHERY_TARGET_HALF_40));

    // 半靶第 7 个槽位不存在
    assert(!archery_counts_adjust(counts, 7, 1, 3, ARCHERY_TARGET_HALF_40));

    // 全靶可以用 1..5 环与 0 环
    memset(counts, 0, sizeof(counts));
    assert(archery_counts_adjust(counts, 6, 2, 6, ARCHERY_TARGET_FULL_40));  // 5 环
    assert(archery_counts_adjust(counts, 10, 2, 6, ARCHERY_TARGET_FULL_40)); // 1 环
    assert(archery_counts_adjust(counts, 11, 2, 6, ARCHERY_TARGET_FULL_40)); // 0 环
    assert(archery_counts_total(counts) == 6);
    assert(archery_counts_points(counts, ARCHERY_TARGET_FULL_40) == 5 * 2 + 1 * 2 + 0);

    // 剩余额度 1 时 delta=3 只能加 1
    uint8_t partial[ARCHERY_RING_SLOT_MAX] = {1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
    assert(archery_counts_adjust(partial, 5, 3, 6, ARCHERY_TARGET_HALF_40));
    assert(partial[5] == 1);
    assert(archery_counts_total(partial) == 6);
    assert(!archery_counts_adjust(partial, 6, 1, 6, ARCHERY_TARGET_HALF_40));
}

static void test_begin_and_expand(void) {
    archery_session_t session;
    archery_session_begin(&session, 2, 3, ARCHERY_TARGET_HALF_40);
    assert(session.groups == 2 && session.arrows == 3);
    assert(session.target == ARCHERY_TARGET_HALF_40);
    // 累计从 0 开始：不再预填 10 环
    assert(archery_session_total_points(&session) == 0);
    assert(archery_session_average_x10(&session) == 0);
    assert(archery_session_max_points(&session) == 60);

    uint8_t counts[ARCHERY_RING_SLOT_MAX] = {2, 1, 0};
    assert(archery_session_apply_group_counts(&session, counts));
    assert(archery_session_group_points(&session, 0) == 30);
    assert(archery_session_group_x(&session, 0) == 2);
    assert(archery_session_ring_count(&session, 0, 0) == 2);
    assert(archery_session_ring_count(&session, 0, 1) == 1);
    assert(archery_session_ring_count(&session, 0, 2) == 0);

    // 合计不等于每组箭数时必须拒绝
    uint8_t bad[ARCHERY_RING_SLOT_MAX] = {1, 1, 0};
    archery_session_t before = session;
    assert(!archery_session_apply_group_counts(&session, bad));
    assert(memcmp(&before, &session, sizeof(session)) == 0);

    // 非法靶型回落到 40 半靶
    archery_session_begin(&session, 1, 1, 99);
    assert(session.target == ARCHERY_TARGET_HALF_40);
}

static void test_full_target_session(void) {
    archery_session_t session;
    archery_session_begin(&session, 1, 6, ARCHERY_TARGET_FULL_80);
    assert(session.target == ARCHERY_TARGET_FULL_80);

    // 全靶：X1、10 2、5 1、1 1、0 1 -> 10+20+5+1+0 = 36，内十 1，脱靶 1
    uint8_t counts[ARCHERY_RING_SLOT_MAX] = {
        1, 2, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1,
    };
    // 槽位: 0=X,1=10,2=9,3=8,4=7,5=6,6=5,7=4,8=3,9=2,10=1,11=0
    assert(archery_session_apply_group_counts(&session, counts));
    assert(archery_session_group_points(&session, 0) == 36);
    assert(archery_session_group_x(&session, 0) == 1);
    assert(archery_session_group_miss(&session, 0) == 1);
    assert(archery_session_ring_count(&session, 0, 6) == 1);   // 5 环
    assert(archery_session_ring_count(&session, 0, 10) == 1);  // 1 环
    assert(archery_session_ring_count(&session, 0, 11) == 1);  // 0 环
    assert(archery_session_total_points(&session) == 36);
}

static void test_face_counts(void) {
    archery_session_t session;
    archery_session_begin(&session, 1, 6, ARCHERY_TARGET_TRIPLE_40H);
    // 上靶：X 1、10 1
    uint8_t face0[ARCHERY_RING_SLOT_MAX] = {1, 1};
    assert(archery_session_apply_face_counts(&session, 0, 2, face0));
    // 中靶：9 环 2 根
    uint8_t face1[ARCHERY_RING_SLOT_MAX] = {0, 0, 2};
    assert(archery_session_apply_face_counts(&session, 2, 2, face1));
    // 下靶：脱靶 2 根
    uint8_t face2[ARCHERY_RING_SLOT_MAX] = {0, 0, 0, 0, 0, 0, 2};
    assert(archery_session_apply_face_counts(&session, 4, 2, face2));

    // 合计不符或越出本组箭数必须拒绝，且不改动会话
    uint8_t bad[ARCHERY_RING_SLOT_MAX] = {1};
    archery_session_t before = session;
    assert(!archery_session_apply_face_counts(&session, 0, 2, bad));
    assert(!archery_session_apply_face_counts(&session, 5, 2, face0));
    assert(memcmp(&before, &session, sizeof(session)) == 0);

    assert(archery_session_group_points(&session, 0) == 10 + 10 + 9 + 9);
    assert(archery_session_group_x(&session, 0) == 1);
    assert(archery_session_group_miss(&session, 0) == 2);
    assert(archery_session_ring_count(&session, 0, 0) == 1);  // X
    assert(archery_session_ring_count(&session, 0, 2) == 2);  // 9 环
    // 单靶面也可用 0 根（例如某靶面本轮不打）
    uint8_t empty[ARCHERY_RING_SLOT_MAX] = {0};
    assert(archery_session_apply_face_counts(&session, 0, 0, empty));
}

static void test_totals_and_steps(void) {
    archery_session_t session;
    archery_session_begin(&session, 2, 6, ARCHERY_TARGET_HALF_40);
    // 第一组：X1,10 2,9 1,M 2 -> 10+20+9+0 = 39，内十 1，脱靶 2
    uint8_t first[ARCHERY_RING_SLOT_MAX] = {1, 2, 1, 0, 0, 0, 2};
    assert(archery_session_apply_group_counts(&session, first));
    assert(archery_session_group_points(&session, 0) == 39);
    assert(archery_session_group_x(&session, 0) == 1);
    assert(archery_session_group_miss(&session, 0) == 2);

    assert(archery_session_commit_group(&session));
    assert(session.group == 1);

    // 第二组：6 根 8 环 -> 48
    uint8_t second[ARCHERY_RING_SLOT_MAX] = {0, 0, 0, 6, 0, 0, 0};
    assert(archery_session_apply_group_counts(&session, second));
    assert(archery_session_total_points(&session) == 87);
    assert(archery_session_total_x(&session) == 1);
    assert(archery_session_total_miss(&session) == 2);
    assert(archery_session_best_group(&session) == 48);
    assert(archery_session_average_x10(&session) == 73);
    assert(!archery_session_commit_group(&session));
    assert(archery_session_complete(&session));

    assert(archery_session_group_points(&session, 2) == 0);
    assert(archery_session_ring_count(&session, 9, 0) == 0);
    assert(!archery_session_apply_group_counts(&session, second));
}

static void test_clamp(void) {
    assert(archery_groups_clamp(0) == ARCHERY_GROUPS_MIN);
    assert(archery_groups_clamp(99) == ARCHERY_GROUPS_MAX);
    assert(archery_arrows_clamp(-3) == ARCHERY_ARROWS_MIN);
    archery_session_t session;
    archery_session_begin(&session, 99, 0, ARCHERY_TARGET_HALF_40);
    assert(session.groups == ARCHERY_GROUPS_MAX);
    assert(session.arrows == ARCHERY_ARROWS_MIN);
    assert(archery_session_arrow_count(&session) == ARCHERY_GROUPS_MAX);
}

int main(void) {
    test_target_tables();
    test_score_table();
    test_counts_helpers();
    test_begin_and_expand();
    test_full_target_session();
    test_face_counts();
    test_totals_and_steps();
    test_clamp();
    puts("archery score/session logic tests: PASS");
    return 0;
}
