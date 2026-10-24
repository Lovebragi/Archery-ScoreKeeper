// main/archery_session.h —— 一次训练（若干组 × 每组若干箭）的纯状态与统计。
//
// 组内录入按"每个环值有几根箭"统计：进入某组后先给各环分配箭数，数量合计
// 等于每组箭数时提交本组，然后把环值展开成逐箭成绩。环值集合由靶型决定
// （半靶 6~10 + X，全靶 0~10 + X）。累计环数从 0 开始，不做任何"满环预扣"。
// 本模块不含 UI、NVS 或任务代码，可主机单测。
#pragma once

#include "archery_score.h"
#include <stdbool.h>
#include <stdint.h>

// 配置边界：默认 6 组 × 6 箭，与 40 半靶常见训练量一致。
#define ARCHERY_DEFAULT_GROUPS 6u
#define ARCHERY_DEFAULT_ARROWS 6u
#define ARCHERY_GROUPS_MIN 1u
#define ARCHERY_GROUPS_MAX 12u
#define ARCHERY_ARROWS_MIN 1u
#define ARCHERY_ARROWS_MAX 12u

typedef struct {
    uint8_t groups;  // 本次训练组数 1..12
    uint8_t arrows;  // 每组箭数 1..12
    uint8_t target;  // 靶型：archery_target_t
    uint8_t group;   // 当前组下标；== groups 表示本次训练已提交完最后一组
    uint8_t scores[ARCHERY_GROUPS_MAX][ARCHERY_ARROWS_MAX];
} archery_session_t;

// 把任意输入钳制到合法范围，供设置页和损坏数据修复使用。
uint8_t archery_groups_clamp(int value);
uint8_t archery_arrows_clamp(int value);

// 开一次新训练：钳制组数/箭数，记录靶型。所有箭先记 0 环，等本组统计提交时
// 再按各环箭数展开，因此累计环数始终从 0 开始累加。
void archery_session_begin(archery_session_t *session, int groups, int arrows,
                           uint8_t target);

// 统计。group 越界或未使用的位置不计入。
uint8_t archery_session_group_points(const archery_session_t *session, uint8_t group);
uint8_t archery_session_group_x(const archery_session_t *session, uint8_t group);
uint8_t archery_session_group_miss(const archery_session_t *session, uint8_t group);
uint16_t archery_session_total_points(const archery_session_t *session);
uint8_t archery_session_total_x(const archery_session_t *session);
uint8_t archery_session_total_miss(const archery_session_t *session);
uint8_t archery_session_arrow_count(const archery_session_t *session);
uint16_t archery_session_max_points(const archery_session_t *session);
// 平均每箭环数，放大 10 倍（四舍五入）：57 环 / 6 箭 -> 95 表示 9.5 环。
uint16_t archery_session_average_x10(const archery_session_t *session);
// 最好的一组环数；没有可统计的组时返回 0。
uint8_t archery_session_best_group(const archery_session_t *session);
// 当前组某个环值槽位上的箭数（槽位含义由靶型决定）。
uint8_t archery_session_ring_count(const archery_session_t *session,
                                   uint8_t group, uint8_t slot);

// 各环箭数的小工具（数组长度固定 ARCHERY_RING_SLOT_MAX）。
uint8_t archery_counts_total(const uint8_t *counts);
// 按靶型把各环箭数换算成本组环数（提交前在录入页实时显示）。
uint16_t archery_counts_points(const uint8_t *counts, uint8_t target);
// 调整某个槽位的箭数：数量钳制在 0..arrows，且增加后总数不得超过 arrows。
// 槽位越界（超出该靶型槽位数）返回 false。
bool archery_counts_adjust(uint8_t *counts, uint8_t slot, int delta, uint8_t arrows,
                           uint8_t target);

// 把本组各环箭数展开成逐箭成绩。合计必须等于 session->arrows，否则返回
// false 且不修改会话。
bool archery_session_apply_group_counts(archery_session_t *session,
                                        const uint8_t *counts);

// 把某个靶面的各环箭数展开到本组成绩的第 arrow_offset 支箭起（三联靶按
// 上/中/下三个靶面分别录入时使用）。合计必须等于 face_arrows，且不能越出
// 本组箭数；否则返回 false 且不修改会话。
bool archery_session_apply_face_counts(archery_session_t *session,
                                       uint8_t arrow_offset, uint8_t face_arrows,
                                       const uint8_t *counts);

// 提交当前组。返回 true 表示还有下一组（光标移到下一组），
// 返回 false 表示本次训练已完成（group == groups）。
bool archery_session_commit_group(archery_session_t *session);

// 是否已提交完最后一组。
bool archery_session_complete(const archery_session_t *session);
