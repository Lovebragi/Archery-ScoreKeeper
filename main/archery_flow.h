// main/archery_flow.h —— 页面与按键的纯状态机。
//
// UI 层只负责把 flow 画出来；持久化和计时等副作用由 archery_app.c 根据返回的
// action 执行。这样页面流转、边界钳制和按钮语义都可以在主机上单测。
#pragma once

#include "archery_session.h"
#include "archery_settings.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ARCHERY_PAGE_HOME = 0,
    ARCHERY_PAGE_SETUP,
    ARCHERY_PAGE_ENTRY,
    ARCHERY_PAGE_SUMMARY,
    ARCHERY_PAGE_FINISH,
    ARCHERY_PAGE_HISTORY,
    ARCHERY_PAGE_DETAIL,
    ARCHERY_PAGE_CLEAR_CONFIRM,
    ARCHERY_PAGE_ABORT_CONFIRM,
    ARCHERY_PAGE_SETTINGS,
} archery_page_t;

// 主页入口数：开始训练 / 历史记录 / 设置。
#define ARCHERY_HOME_ITEMS 3u

// 录入页行数上限：最多 12 个环值槽位 + 1 行"确认提交"。
#define ARCHERY_ENTRY_ROWS_MAX (ARCHERY_RING_SLOT_MAX + 1u)
// 三联靶的靶面数：上 / 中 / 下。
#define ARCHERY_FACES 3u
// 设置页行数上限：三联靶比单靶多出上/中/下三行箭数。
#define ARCHERY_SETUP_ROWS_MAX 6u

typedef enum {
    ARCHERY_KEY_UP = 0,
    ARCHERY_KEY_DOWN,
    ARCHERY_KEY_OK,
    ARCHERY_KEY_OK_LONG,
} archery_key_t;

// 物理按键在机身侧边按键列中的位置（自然方向自上而下：上、下、确定）。
typedef enum {
    ARCHERY_BUTTON_UP = 0,
    ARCHERY_BUTTON_DOWN,
    ARCHERY_BUTTON_OK,
} archery_button_t;

// 物理按键 → 逻辑键。整机倒装时按键列的上下顺序也反了，rotate_roles 会把角色
// 整体旋转 180°：物理上键当确定、物理下键仍是下、物理确定键当上。
archery_key_t archery_flow_button_key(archery_button_t button, bool rotate_roles);

// 该物理键在逻辑上是否承担"确定"（长按 = 返回/取消）。
bool archery_flow_button_is_ok(archery_button_t button, bool rotate_roles);

typedef enum {
    ARCHERY_ACT_NONE = 0,
    ARCHERY_ACT_SESSION_START,  // 开始新训练：上层记录起始时间
    ARCHERY_ACT_SAVE_RECORD,    // 训练完成：上层把会话写入历史
    ARCHERY_ACT_CLEAR_HISTORY,  // 清空全部历史记录
    ARCHERY_ACT_SETTINGS_CHANGED,  // 设置项改了：上层立即应用亮度/息屏时间
    ARCHERY_ACT_SETTINGS_SAVE,     // 离开设置页：上层把设置写入 NVS
} archery_act_t;

typedef struct {
    archery_page_t page;
    archery_page_t abort_return;  // 取消"放弃训练"后回到的页面
    archery_session_t session;
    uint8_t setup_groups;   // 设置页当前值，默认 6
    uint8_t setup_arrows;   // 每组箭数，默认 6
    uint8_t setup_row;      // 0 = 组数, 1 = 每组箭数, 2..4 = 上/中/下, 最后 = 开始
    uint8_t setup_face[ARCHERY_FACES];  // 三联靶上/中/下各几根箭
    uint8_t home_select;    // 0 = 开始训练, 1 = 历史记录, 2 = 设置
    uint16_t history_count; // 由上层同步：本地已有记录数
    uint8_t history_cursor; // 0..count，count 表示"清空全部记录"行
    uint8_t detail_group;   // 历史详情当前查看的组
    uint8_t detail_groups;  // 当前查看记录的总组数（进入详情时由上层设置）
    bool confirm_yes;       // 确认页当前选择
    bool save_failed;       // 上层写入历史失败时置位，完成页据此提示
    uint8_t counts[ARCHERY_FACES][ARCHERY_RING_SLOT_MAX];  // 各靶面各环箭数
    uint8_t entry_face;     // 三联靶当前录入的靶面 0..2
    uint8_t entry_row;      // 0..槽位数-1 为环值行，槽位数为提交行
    archery_settings_t settings;  // 靶型与省电设置（由上层从 NVS 载入）
    uint8_t settings_row;   // 0 = 靶型, 1 = 亮度, 2 = 息屏时间, 3 = 返回
} archery_flow_t;

void archery_flow_init(archery_flow_t *flow);

// 该靶型是否按上/中/下三个靶面分别录入（目前只有三联靶）。
bool archery_flow_target_has_faces(uint8_t target);

// 三联靶设置页里上/中/下箭数的合计。
uint8_t archery_flow_setup_faces_total(const archery_flow_t *flow);

// 用 NVS 里读到的设置覆盖 flow 中的副本（开机时调用，设置页直接改这个副本）。
void archery_flow_set_settings(archery_flow_t *flow, const archery_settings_t *settings);

// 处理一次按键：更新状态并返回需要上层执行的副作用。
archery_act_t archery_flow_handle(archery_flow_t *flow, archery_key_t key);

// 历史记录数量变化（写入新记录或清空后）由上层同步进来，重新钳制光标。
void archery_flow_set_history_count(archery_flow_t *flow, uint16_t count);
