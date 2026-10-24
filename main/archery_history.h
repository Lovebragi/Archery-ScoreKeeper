// main/archery_history.h —— 历史记录的数据格式与环形槽位，纯逻辑。
//
// NVS 读写在 archery_store.c；这里只负责一条记录的内存表示、可校验的
// 二进制编码，以及"最近 N 次"环形缓冲的槽位计算，以便主机单测。
// 记录里保存靶型，因此历史详情能按同一靶型的环值表展示。
#pragma once

#include "archery_session.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 本地最多保存最近 50 次训练。
#define ARCHERY_HISTORY_CAPACITY 50u

#define ARCHERY_RECORD_MAX_ARROWS (ARCHERY_GROUPS_MAX * ARCHERY_ARROWS_MAX)
#define ARCHERY_RECORD_PACKED_BYTES (ARCHERY_RECORD_MAX_ARROWS / 2u)
// v2：magic + 版本 + 组数 + 箭数 + 靶型 + 序号(2) + 用时(2) = 9 字节头。
#define ARCHERY_RECORD_HEADER_BYTES 9u
#define ARCHERY_RECORD_VERSION 2u
#define ARCHERY_RECORD_VERSION_V1 1u
#define ARCHERY_RECORD_MAGIC 0xA7u
// 9 字节头 + 72 字节环值 + 2 字节 CRC（旧版头部少 1 字节，仍复用同一缓冲）。
#define ARCHERY_RECORD_BUFFER_BYTES (10u + ARCHERY_RECORD_PACKED_BYTES + 2u)
// 不存在的箭位（越界查询）统一返回的哨兵值。
#define ARCHERY_RECORD_SCORE_INVALID 0xFFu

typedef struct {
    uint16_t seq;         // 第几次训练，从 1 开始递增（NVS 里跨重启保留）
    uint16_t duration_s;  // 本次训练用时，秒
    uint8_t groups;       // 1..12
    uint8_t arrows;       // 每组箭数 1..12
    uint8_t target;       // 靶型：archery_target_t
    uint8_t packed[ARCHERY_RECORD_PACKED_BYTES];  // 每字节两个 4bit 环值
} archery_record_t;

typedef struct {
    uint16_t count;       // 已保存记录数 0..50（加载时按实际可解码记录重算）
    uint16_t next_seq;    // 下一条记录使用的序号（>= 1）
} archery_history_meta_t;

void archery_history_meta_init(archery_history_meta_t *meta);

// 记录成功写入后调用：推进序号与计数（计数上限 50）。
void archery_history_meta_after_save(archery_history_meta_t *meta);

// seq 对应的 NVS 槽位：0..49，按 (seq-1) % 50 固定映射，便于重启后定位。
// seq == 0 返回 0xFF。
uint8_t archery_history_slot_for_seq(uint16_t seq);

// 第 newest_index 新（0 = 最新）的历史记录序号；没有该记录返回 0。
uint16_t archery_history_seq_newest(const archery_history_meta_t *meta,
                                    uint16_t newest_index);

// 从会话生成一条记录（只在整组提交完成后调用）。
void archery_record_from_session(archery_record_t *record,
                                 const archery_session_t *session,
                                 uint16_t seq, uint16_t duration_s);

// 按 (group, arrow) 取环值，越界返回 ARCHERY_RECORD_SCORE_INVALID。
uint8_t archery_record_score(const archery_record_t *record,
                             uint8_t group, uint8_t arrow);
uint8_t archery_record_group_points(const archery_record_t *record, uint8_t group);
uint8_t archery_record_group_x(const archery_record_t *record, uint8_t group);
uint8_t archery_record_group_miss(const archery_record_t *record, uint8_t group);
// 该组某个环值槽位上的箭数（槽位含义由记录里的靶型决定）。
uint8_t archery_record_ring_count(const archery_record_t *record,
                                  uint8_t group, uint8_t slot);
uint16_t archery_record_total_points(const archery_record_t *record);
uint8_t archery_record_total_x(const archery_record_t *record);
uint8_t archery_record_total_miss(const archery_record_t *record);
uint8_t archery_record_best_group(const archery_record_t *record);
uint8_t archery_record_arrow_count(const archery_record_t *record);

// 编码：返回写入字节数；容量不足或数据非法返回 0。
size_t archery_record_encode(const archery_record_t *record,
                             uint8_t *buffer, size_t capacity);
// 解码：magic/版本/长度/CRC/环值逐项校验，任何一项不符返回 false。
// 兼容旧版（无靶型字段）记录，按 40 半靶处理。
bool archery_record_decode(archery_record_t *record,
                           const uint8_t *buffer, size_t length);
