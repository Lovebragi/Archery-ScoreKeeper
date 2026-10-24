// tests/test_archery_flow.c —— 页面/按键状态机测试（按环值统计录入）。
#include "archery_flow.h"

#include <assert.h>
#include <stdio.h>

static void press(archery_flow_t *flow, archery_key_t key) {
    (void)archery_flow_handle(flow, key);
}

static void start_session(archery_flow_t *flow, int groups, int arrows) {
    // 主页 -> 开始训练 -> 设置页
    press(flow, ARCHERY_KEY_UP);
    press(flow, ARCHERY_KEY_OK);
    assert(flow->page == ARCHERY_PAGE_SETUP);
    for (int i = 0; i < 40; i++) press(flow, ARCHERY_KEY_DOWN);
    for (int i = 0; i < groups - 1; i++) press(flow, ARCHERY_KEY_UP);
    press(flow, ARCHERY_KEY_OK);  // 到"每组箭数"
    for (int i = 0; i < 40; i++) press(flow, ARCHERY_KEY_DOWN);
    for (int i = 0; i < arrows - 1; i++) press(flow, ARCHERY_KEY_UP);
    press(flow, ARCHERY_KEY_OK);  // 到"开始记录"
    assert(flow->setup_row == 2);
    assert(archery_flow_handle(flow, ARCHERY_KEY_OK) == ARCHERY_ACT_SESSION_START);
    assert(flow->page == ARCHERY_PAGE_ENTRY);
    assert(flow->session.groups == groups && flow->session.arrows == arrows);
}

// 逐行下移，直到停在提交行（环值已满时会直接跳到提交行）。
static void goto_commit_row(archery_flow_t *flow) {
    const uint8_t commit = archery_target_slot_count(flow->session.target);
    while (flow->entry_row < commit) {
        press(flow, ARCHERY_KEY_OK);
    }
    assert(flow->entry_row == commit);
}

static void test_home_and_setup(void) {
    archery_flow_t flow;
    archery_flow_init(&flow);
    assert(flow.page == ARCHERY_PAGE_HOME);
    assert(flow.setup_groups == ARCHERY_DEFAULT_GROUPS);
    assert(flow.setup_arrows == ARCHERY_DEFAULT_ARROWS);

    press(&flow, ARCHERY_KEY_DOWN);
    assert(flow.home_select == 1);
    press(&flow, ARCHERY_KEY_DOWN);
    assert(flow.home_select == 2);  // 主页三项
    press(&flow, ARCHERY_KEY_DOWN);
    assert(flow.home_select == 2);  // 到底钳制
    press(&flow, ARCHERY_KEY_UP);
    press(&flow, ARCHERY_KEY_UP);
    assert(flow.home_select == 0);

    press(&flow, ARCHERY_KEY_OK);
    assert(flow.page == ARCHERY_PAGE_SETUP);
    assert(flow.setup_row == 0);

    press(&flow, ARCHERY_KEY_UP);
    assert(flow.setup_groups == 7);
    for (int i = 0; i < 20; i++) press(&flow, ARCHERY_KEY_UP);
    assert(flow.setup_groups == ARCHERY_GROUPS_MAX);
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.setup_row == 1);
    press(&flow, ARCHERY_KEY_DOWN);
    assert(flow.setup_arrows == 5);

    // 长按确定逐项回退，在首项再长按才回主页
    press(&flow, ARCHERY_KEY_OK_LONG);
    assert(flow.setup_row == 0);
    press(&flow, ARCHERY_KEY_OK_LONG);
    assert(flow.page == ARCHERY_PAGE_HOME);
}

static void test_ring_count_entry(void) {
    archery_flow_t flow;
    archery_flow_init(&flow);
    start_session(&flow, 2, 3);

    // 新一组：所有环值计数为 0，累计也从 0 开始
    assert(flow.entry_row == 0);
    assert(archery_counts_total(flow.counts[0]) == 0);
    assert(archery_session_total_points(&flow.session) == 0);

    // 第一行 X：上 -> 1
    press(&flow, ARCHERY_KEY_UP);
    assert(flow.counts[0][0] == 1);
    // 下 -> 0，再下仍为 0
    press(&flow, ARCHERY_KEY_DOWN);
    press(&flow, ARCHERY_KEY_DOWN);
    assert(flow.counts[0][0] == 0);

    // 记 X1、10 1、9 1（共 3 根）
    press(&flow, ARCHERY_KEY_UP);
    press(&flow, ARCHERY_KEY_OK);   // 到 10 环行
    assert(flow.entry_row == 1);
    press(&flow, ARCHERY_KEY_UP);
    press(&flow, ARCHERY_KEY_OK);   // 到 9 环行
    press(&flow, ARCHERY_KEY_UP);
    assert(archery_counts_total(flow.counts[0]) == 3);

    // 已记满：继续按上不增加
    press(&flow, ARCHERY_KEY_UP);
    assert(archery_counts_total(flow.counts[0]) == 3);

    goto_commit_row(&flow);
    press(&flow, ARCHERY_KEY_OK);  // 合计正确，提交
    assert(flow.page == ARCHERY_PAGE_SUMMARY);
    assert(flow.session.group == 1);
    assert(archery_session_total_points(&flow.session) == 29);
    assert(archery_session_group_x(&flow.session, 0) == 1);

    // 小结 -> 下一组：计数与光标复位
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.page == ARCHERY_PAGE_ENTRY);
    assert(flow.entry_row == 0);
    assert(archery_counts_total(flow.counts[0]) == 0);

    // 第二组只记 6 环 3 根
    for (int i = 0; i < 5; i++) press(&flow, ARCHERY_KEY_OK);  // 到 6 环行
    assert(flow.entry_row == 5);
    press(&flow, ARCHERY_KEY_UP);
    press(&flow, ARCHERY_KEY_UP);
    press(&flow, ARCHERY_KEY_UP);
    assert(flow.counts[0][5] == 3);
    goto_commit_row(&flow);
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.page == ARCHERY_PAGE_SUMMARY);
    assert(archery_session_complete(&flow.session));
    assert(archery_session_total_points(&flow.session) == 29 + 18);

    // 最后一组提交后确定 -> 保存并进入完成页
    flow.save_failed = true;  // 该标记应在新一轮完成时清除
    assert(archery_flow_handle(&flow, ARCHERY_KEY_OK) == ARCHERY_ACT_SAVE_RECORD);
    assert(flow.page == ARCHERY_PAGE_FINISH);
    assert(!flow.save_failed);

    press(&flow, ARCHERY_KEY_OK);
    assert(flow.page == ARCHERY_PAGE_HOME);
}

static void test_incomplete_group_cannot_commit(void) {
    archery_flow_t flow;
    archery_flow_init(&flow);
    start_session(&flow, 1, 6);

    goto_commit_row(&flow);
    // 一根都没记：按确定不提交，而是回到第一行继续填
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.page == ARCHERY_PAGE_ENTRY);
    assert(flow.entry_row == 0);

    // 长按确定在环值行是"返回上一项"（首行时为放弃确认）
    press(&flow, ARCHERY_KEY_OK_LONG);
    assert(flow.page == ARCHERY_PAGE_ABORT_CONFIRM);
    press(&flow, ARCHERY_KEY_OK);  // 继续训练
    assert(flow.page == ARCHERY_PAGE_ENTRY);
    assert(flow.entry_row == 0);

    // 补齐 6 根（6 环行）后可以提交
    for (int i = 0; i < 5; i++) press(&flow, ARCHERY_KEY_OK);
    assert(flow.entry_row == 5);
    for (int i = 0; i < 6; i++) press(&flow, ARCHERY_KEY_UP);
    assert(flow.counts[0][5] == 6);
    goto_commit_row(&flow);
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.page == ARCHERY_PAGE_SUMMARY);
    assert(archery_session_total_points(&flow.session) == 36);
}

static void test_abort_paths(void) {
    archery_flow_t flow;
    archery_flow_init(&flow);
    start_session(&flow, 1, 2);

    press(&flow, ARCHERY_KEY_OK_LONG);
    assert(flow.page == ARCHERY_PAGE_ABORT_CONFIRM);
    assert(!flow.confirm_yes);
    // 默认"继续训练"：回到录入页
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.page == ARCHERY_PAGE_ENTRY);
    // 已填计数保留
    press(&flow, ARCHERY_KEY_UP);
    assert(flow.counts[0][0] == 1);

    // 选择"放弃本次训练"：回到主页
    press(&flow, ARCHERY_KEY_OK_LONG);
    press(&flow, ARCHERY_KEY_DOWN);
    assert(flow.confirm_yes);
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.page == ARCHERY_PAGE_HOME);

    // 小结页长按进入放弃确认，继续训练回到小结
    start_session(&flow, 1, 1);
    press(&flow, ARCHERY_KEY_UP);  // X 1 根
    goto_commit_row(&flow);
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.page == ARCHERY_PAGE_SUMMARY);
    press(&flow, ARCHERY_KEY_OK_LONG);
    assert(flow.page == ARCHERY_PAGE_ABORT_CONFIRM);
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.page == ARCHERY_PAGE_SUMMARY);
}

static void test_button_role_rotation(void) {
    // 自然方向：物理上/下/确定就是逻辑上/下/确定
    assert(archery_flow_button_key(ARCHERY_BUTTON_UP, false) == ARCHERY_KEY_UP);
    assert(archery_flow_button_key(ARCHERY_BUTTON_DOWN, false) == ARCHERY_KEY_DOWN);
    assert(archery_flow_button_key(ARCHERY_BUTTON_OK, false) == ARCHERY_KEY_OK);
    assert(archery_flow_button_is_ok(ARCHERY_BUTTON_OK, false));
    assert(!archery_flow_button_is_ok(ARCHERY_BUTTON_UP, false));

    // 整机倒装：角色旋转 180°，物理上<->确定，下不变
    assert(archery_flow_button_key(ARCHERY_BUTTON_UP, true) == ARCHERY_KEY_OK);
    assert(archery_flow_button_key(ARCHERY_BUTTON_DOWN, true) == ARCHERY_KEY_DOWN);
    assert(archery_flow_button_key(ARCHERY_BUTTON_OK, true) == ARCHERY_KEY_UP);
    assert(archery_flow_button_is_ok(ARCHERY_BUTTON_UP, true));
    assert(!archery_flow_button_is_ok(ARCHERY_BUTTON_OK, true));
}

static void test_full_tally_shortcut(void) {
    archery_flow_t flow;
    archery_flow_init(&flow);
    start_session(&flow, 1, 6);

    // 在 10 环行一次记满 6 根：确定应直接跳到提交行，而不是逐行下移
    press(&flow, ARCHERY_KEY_OK);  // 到 10 环行
    assert(flow.entry_row == 1);
    for (int i = 0; i < 6; i++) press(&flow, ARCHERY_KEY_UP);
    assert(flow.counts[0][1] == 6);
    assert(archery_counts_total(flow.counts[0]) == flow.session.arrows);

    press(&flow, ARCHERY_KEY_OK);
    assert(flow.page == ARCHERY_PAGE_ENTRY);
    assert(flow.entry_row == archery_target_slot_count(flow.session.target));

    // 提交行再按一次确定即提交
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.page == ARCHERY_PAGE_SUMMARY);
    assert(archery_session_total_points(&flow.session) == 60);
}

static void test_full_target_entry(void) {
    archery_flow_t flow;
    archery_flow_init(&flow);
    archery_settings_t settings;
    archery_settings_defaults(&settings);
    settings.target = ARCHERY_TARGET_FULL_40;
    archery_flow_set_settings(&flow, &settings);

    start_session(&flow, 1, 2);
    assert(flow.session.target == ARCHERY_TARGET_FULL_40);
    assert(archery_target_slot_count(flow.session.target) == 12);

    // 第 1 槽是 10 环，记 1 根
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.entry_row == 1);
    press(&flow, ARCHERY_KEY_UP);
    assert(flow.counts[0][1] == 1);
    press(&flow, ARCHERY_KEY_DOWN);
    assert(flow.counts[0][1] == 0);
    press(&flow, ARCHERY_KEY_UP);

    // 走到第 11 槽（0 环），记 1 根；此时合计等于每组箭数
    for (int i = 0; i < 10; i++) press(&flow, ARCHERY_KEY_OK);
    assert(flow.entry_row == 11);
    press(&flow, ARCHERY_KEY_UP);
    assert(flow.counts[0][11] == 1);
    assert(archery_counts_total(flow.counts[0]) == 2);

    // 记满后确定直接跳到提交行，再确定提交
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.entry_row == 12);
    assert(flow.page == ARCHERY_PAGE_ENTRY);
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.page == ARCHERY_PAGE_SUMMARY);
    assert(archery_session_group_points(&flow.session, 0) == 10);
    assert(archery_session_group_miss(&flow.session, 0) == 1);
}

static void test_triple_target_setup_and_entry(void) {
    archery_flow_t flow;
    archery_flow_init(&flow);
    archery_settings_t settings;
    archery_settings_defaults(&settings);
    settings.target = ARCHERY_TARGET_TRIPLE_40H;
    archery_flow_set_settings(&flow, &settings);

    // 主页 -> 新训练
    press(&flow, ARCHERY_KEY_UP);
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.page == ARCHERY_PAGE_SETUP);
    assert(flow.setup_row == 0);
    assert(flow.setup_groups == 6 && flow.setup_arrows == 6);
    // 默认上/中/下 2/2/2
    assert(flow.setup_face[0] == 2 && flow.setup_face[1] == 2 && flow.setup_face[2] == 2);

    // 组数改成 1，每组箭数改成 4 -> 自动重分为 2/1/1
    for (int i = 0; i < 5; i++) press(&flow, ARCHERY_KEY_DOWN);
    assert(flow.setup_groups == 1);
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.setup_row == 1);
    press(&flow, ARCHERY_KEY_DOWN);
    press(&flow, ARCHERY_KEY_DOWN);
    assert(flow.setup_arrows == 4);
    assert(flow.setup_face[0] == 2 && flow.setup_face[1] == 1 && flow.setup_face[2] == 1);

    // 上靶改成 3 -> 合计 5 != 4，不允许开始
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.setup_row == 2);
    press(&flow, ARCHERY_KEY_UP);
    assert(flow.setup_face[0] == 3);
    assert(archery_flow_setup_faces_total(&flow) == 5);
    press(&flow, ARCHERY_KEY_OK);  // 中靶
    press(&flow, ARCHERY_KEY_OK);  // 下靶
    press(&flow, ARCHERY_KEY_OK);  // 开始
    assert(flow.setup_row == 5);
    assert(archery_flow_handle(&flow, ARCHERY_KEY_OK) == ARCHERY_ACT_NONE);
    assert(flow.page == ARCHERY_PAGE_SETUP);

    // 长按退回上靶行，改回 2 -> 合计 4
    press(&flow, ARCHERY_KEY_OK_LONG);
    press(&flow, ARCHERY_KEY_OK_LONG);
    press(&flow, ARCHERY_KEY_OK_LONG);
    assert(flow.setup_row == 2);
    press(&flow, ARCHERY_KEY_DOWN);
    assert(flow.setup_face[0] == 2);
    assert(archery_flow_setup_faces_total(&flow) == 4);

    // 重新走到开始
    press(&flow, ARCHERY_KEY_OK);
    press(&flow, ARCHERY_KEY_OK);
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.setup_row == 5);
    assert(archery_flow_handle(&flow, ARCHERY_KEY_OK) == ARCHERY_ACT_SESSION_START);
    assert(flow.page == ARCHERY_PAGE_ENTRY);
    assert(flow.session.target == ARCHERY_TARGET_TRIPLE_40H);
    assert(flow.session.arrows == 4);
    assert(flow.entry_face == 0);

    // 上靶 2 根：10 环 1 根、9 环 1 根
    press(&flow, ARCHERY_KEY_OK);  // X 行 -> 10 环行
    press(&flow, ARCHERY_KEY_UP);
    press(&flow, ARCHERY_KEY_OK);  // 9 环行
    press(&flow, ARCHERY_KEY_UP);
    assert(archery_counts_total(flow.counts[0]) == 2);
    press(&flow, ARCHERY_KEY_OK);  // 记满 -> 提交行
    assert(flow.entry_row == archery_target_slot_count(flow.session.target));
    // 提交上靶 -> 自动切到中靶
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.entry_face == 1);
    assert(flow.entry_row == 0);

    // 中靶 1 根：8 环
    for (int i = 0; i < 3; i++) press(&flow, ARCHERY_KEY_OK);  // 到 8 环行
    assert(flow.entry_row == 3);
    press(&flow, ARCHERY_KEY_UP);
    press(&flow, ARCHERY_KEY_OK);  // 记满 -> 提交行
    press(&flow, ARCHERY_KEY_OK);  // 提交中靶 -> 下靶
    assert(flow.entry_face == 2);

    // 下靶 1 根：脱靶
    for (int i = 0; i < 6; i++) press(&flow, ARCHERY_KEY_OK);  // 到脱靶行
    assert(flow.entry_row == 6);
    press(&flow, ARCHERY_KEY_UP);
    press(&flow, ARCHERY_KEY_OK);  // 记满 -> 提交行
    press(&flow, ARCHERY_KEY_OK);  // 提交整组
    assert(flow.page == ARCHERY_PAGE_SUMMARY);
    assert(flow.session.group == 1);
    // 10 + 9 + 8 + 0 = 27
    assert(archery_session_total_points(&flow.session) == 27);
    assert(archery_session_total_miss(&flow.session) == 1);
}

static void test_settings_page(void) {
    archery_flow_t flow;
    archery_flow_init(&flow);
    // 主页第 3 项进入设置
    press(&flow, ARCHERY_KEY_DOWN);
    press(&flow, ARCHERY_KEY_DOWN);
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.page == ARCHERY_PAGE_SETTINGS);
    assert(flow.settings_row == 0);
    assert(flow.settings.target == ARCHERY_TARGET_HALF_40);
    assert(flow.settings.rotate == 1);
    assert(flow.settings.brightness == 100);
    assert(flow.settings.off_seconds == 30);

    // 靶型：40 半靶 -> 三联靶 -> 40 全靶；到底钳制
    assert(archery_flow_handle(&flow, ARCHERY_KEY_UP) == ARCHERY_ACT_SETTINGS_CHANGED);
    assert(flow.settings.target == ARCHERY_TARGET_TRIPLE_40H);
    assert(archery_flow_handle(&flow, ARCHERY_KEY_UP) == ARCHERY_ACT_SETTINGS_CHANGED);
    assert(flow.settings.target == ARCHERY_TARGET_FULL_40);
    press(&flow, ARCHERY_KEY_DOWN);
    assert(flow.settings.target == ARCHERY_TARGET_TRIPLE_40H);
    press(&flow, ARCHERY_KEY_DOWN);
    assert(flow.settings.target == ARCHERY_TARGET_HALF_40);
    assert(archery_flow_handle(&flow, ARCHERY_KEY_DOWN) == ARCHERY_ACT_NONE);

    // 屏幕方向：默认倒转，调整即切换
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.settings_row == 1);
    assert(archery_flow_handle(&flow, ARCHERY_KEY_UP) == ARCHERY_ACT_SETTINGS_CHANGED);
    assert(flow.settings.rotate == 0);
    assert(archery_flow_handle(&flow, ARCHERY_KEY_DOWN) == ARCHERY_ACT_SETTINGS_CHANGED);
    assert(flow.settings.rotate == 1);

    // 亮度：100 已到顶，向下改到 80
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.settings_row == 2);
    assert(archery_flow_handle(&flow, ARCHERY_KEY_UP) == ARCHERY_ACT_NONE);
    assert(archery_flow_handle(&flow, ARCHERY_KEY_DOWN) == ARCHERY_ACT_SETTINGS_CHANGED);
    assert(flow.settings.brightness == 80);

    // 息屏时间：30 -> 60
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.settings_row == 3);
    assert(archery_flow_handle(&flow, ARCHERY_KEY_UP) == ARCHERY_ACT_SETTINGS_CHANGED);
    assert(flow.settings.off_seconds == 60);

    // 返回行：确定保存并回主页
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.settings_row == 4);
    assert(archery_flow_handle(&flow, ARCHERY_KEY_OK) == ARCHERY_ACT_SETTINGS_SAVE);
    assert(flow.page == ARCHERY_PAGE_HOME);

    // 再次进入：值保留；长按确定同样保存并返回
    press(&flow, ARCHERY_KEY_DOWN);
    press(&flow, ARCHERY_KEY_DOWN);
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.page == ARCHERY_PAGE_SETTINGS);
    assert(flow.settings.brightness == 80);
    press(&flow, ARCHERY_KEY_OK);  // 方向行
    press(&flow, ARCHERY_KEY_OK);  // 亮度行
    press(&flow, ARCHERY_KEY_OK);  // 息屏行
    assert(flow.settings_row == 3);
    assert(archery_flow_handle(&flow, ARCHERY_KEY_DOWN) == ARCHERY_ACT_SETTINGS_CHANGED);
    assert(flow.settings.off_seconds == 30);
    assert(archery_flow_handle(&flow, ARCHERY_KEY_OK_LONG) == ARCHERY_ACT_SETTINGS_SAVE);
    assert(flow.page == ARCHERY_PAGE_HOME);

    // 用 NVS 读到的设置覆盖副本
    archery_settings_t stored = {.target = ARCHERY_TARGET_FULL_122, .rotate = 0,
                                 .brightness = 40, .off_seconds = 120};
    archery_flow_set_settings(&flow, &stored);
    assert(flow.settings.target == ARCHERY_TARGET_FULL_122);
    assert(flow.settings.rotate == 0);
    assert(flow.settings.brightness == 40);
    assert(flow.settings.off_seconds == 120);
    archery_settings_t invalid = {.target = 42, .rotate = 0,
                                  .brightness = 55, .off_seconds = 30};
    archery_flow_set_settings(&flow, &invalid);
    assert(flow.settings.brightness == 40);  // 非法值被忽略
    assert(flow.settings.target == ARCHERY_TARGET_FULL_122);
}

static void test_history_pages(void) {
    archery_flow_t flow;
    archery_flow_init(&flow);
    press(&flow, ARCHERY_KEY_DOWN);
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.page == ARCHERY_PAGE_HISTORY);
    assert(flow.history_cursor == 0);

    // 空历史：确定不会进入详情，光标也不会越过 0
    archery_flow_set_history_count(&flow, 0);
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.page == ARCHERY_PAGE_HISTORY);
    press(&flow, ARCHERY_KEY_DOWN);
    assert(flow.history_cursor == 0);

    // 3 条记录：光标 0..3，第 3 行是"清空全部记录"
    archery_flow_set_history_count(&flow, 3);
    for (int i = 0; i < 5; i++) press(&flow, ARCHERY_KEY_DOWN);
    assert(flow.history_cursor == 3);

    press(&flow, ARCHERY_KEY_UP);
    assert(flow.history_cursor == 2);
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.page == ARCHERY_PAGE_DETAIL);
    assert(flow.detail_group == 0);

    // 详情页：组数由上层写入，上下切换并钳制
    flow.detail_groups = 3;
    for (int i = 0; i < 5; i++) press(&flow, ARCHERY_KEY_DOWN);
    assert(flow.detail_group == 2);
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.page == ARCHERY_PAGE_HISTORY);

    // 进入"清空"行并确认
    press(&flow, ARCHERY_KEY_DOWN);  // 2 -> 3
    assert(flow.history_cursor == 3);
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.page == ARCHERY_PAGE_CLEAR_CONFIRM);
    assert(!flow.confirm_yes);
    press(&flow, ARCHERY_KEY_DOWN);
    assert(flow.confirm_yes);
    assert(archery_flow_handle(&flow, ARCHERY_KEY_OK) == ARCHERY_ACT_CLEAR_HISTORY);
    assert(flow.page == ARCHERY_PAGE_HISTORY);
    assert(flow.history_count == 0);

    // 取消清空
    archery_flow_set_history_count(&flow, 1);
    press(&flow, ARCHERY_KEY_DOWN);  // 0 -> 1（清空行）
    press(&flow, ARCHERY_KEY_OK);
    assert(flow.page == ARCHERY_PAGE_CLEAR_CONFIRM);
    assert(archery_flow_handle(&flow, ARCHERY_KEY_OK) == ARCHERY_ACT_NONE);
    assert(flow.page == ARCHERY_PAGE_HISTORY);
    assert(flow.history_count == 1);

    // 长按返回主页
    press(&flow, ARCHERY_KEY_OK_LONG);
    assert(flow.page == ARCHERY_PAGE_HOME);
}

int main(void) {
    test_home_and_setup();
    test_ring_count_entry();
    test_incomplete_group_cannot_commit();
    test_full_tally_shortcut();
    test_abort_paths();
    test_button_role_rotation();
    test_full_target_entry();
    test_triple_target_setup_and_entry();
    test_settings_page();
    test_history_pages();
    puts("archery flow state-machine tests: PASS");
    return 0;
}
