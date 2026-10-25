// main/archery_history.h —— 历史记录的数据格式与环形槽位，纯逻辑。
//
// NVS 读写在 archery_store.c；这里只负责一条记录的内存表示、可校验的
// 二进制编码，以及"最近 N 次"环形缓冲的槽位计算，以便主机单测。
// 记录里保存靶型与时间戳，因此历史详情能按同一靶型的环值表展示，也能按
// 今天 / 最近 7 天筛选。
#pragma once

#include "archery_clock.h"
#include "archery_session.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 本地最多保存最近 300 次训练（NVS 分区按 256 KB 规划）。
#define ARCHERY_HISTORY_CAPACITY 300u

#define ARCHERY_RECORD_MAX_ARROWS (ARCHERY_GROUPS_MAX * ARCHERY_ARROWS_MAX)
#define ARCHERY_RECORD_PACKED_BYTES (ARCHERY_RECORD_MAX_ARROWS / 2u)
// v3：magic + 版本 + 组数 + 箭数 + 靶型 + 序号(2) + 用时(2) + 时间戳(4) = 13 字节头。
#define ARCHERY_RECORD_HEADER_BYTES 13u
#define ARCHERY_RECORD_VERSION 3u
#define ARCHERY_RECORD_VERSION_V2 2u
#define ARCHERY_RECORD_VERSION_V1 1u
#define ARCHERY_RECORD_MAGIC 0xA7u
// 13 字节头 + 72 字节环值 + 2 字节 CRC；旧版头部更短，仍复用同一缓冲。
#define ARCHERY_RECORD_BUFFER_BYTES (ARCHERY_RECORD_HEADER_BYTES + ARCHERY_RECORD_PACKED_BYTES + 2u)
// 不存在的箭位（越界查询）统一返回的哨兵值。
#define ARCHERY_RECORD_SCORE_INVALID 0xFFu

typedef struct {
    uint16_t seq;         // 第几次训练，从 1 开始递增（NVS 里跨重启保留）
    uint16_t duration_s;  // 本次训练用时，秒
    uint32_t when;        // 训练开始时间（本地墙上时钟，0 表示未知/旧记录）
    uint8_t groups;       // 1..12
    uint8_t arrows;       // 每组箭数 1..12
    uint8_t target;       // 靶型：archery_target_t
    uint8_t packed[ARCHERY_RECORD_PACKED_BYTES];  // 每字节两个 4bit 环值
} archery_record_t;

typedef struct {
    uint16_t count;       // 已保存记录数 0..CAPACITY（加载时按实际可解码记录重算）
    uint16_t next_seq;    // 下一条记录使用的序号（>= 1）
} archery_history_meta_t;

void archery_history_meta_init(archery_history_meta_t *meta);

// 记录成功写入后调用：推进序号与计数（计数上限 CAPACITY）。
void archery_history_meta_after_save(archery_history_meta_t *meta);

// seq 对应的 NVS 槽位：0..CAPACITY-1，按 (seq-1) % CAPACITY 固定映射。
// seq == 0 或容量为 0 时返回 ARCHERY_HISTORY_SLOT_INVALID。
#define ARCHERY_HISTORY_SLOT_INVALID 0xFFFFu
uint16_t archery_history_slot_for_seq(uint16_t seq);

// 第 newest_index 新（0 = 最新）的历史记录序号；没有该记录返回 0。
uint16_t archery_history_seq_newest(const archery_history_meta_t *meta,
                                    uint16_t newest_index);

// 从会话生成一条记录（只在整组提交完成后调用）。when 为训练开始时间。
void archery_record_from_session(archery_record_t *record,
                                 const archery_session_t *session,
                                 uint16_t seq, uint16_t duration_s, uint32_t when);

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

// CSV 导出：表头（UTF-8，逗号分隔）与单行记录；返回写入长度，0 表示缓冲区不足。
const char *archery_record_csv_header(void);
size_t archery_record_csv_row(const archery_record_t *record, uint16_t index,
                              char *buffer, size_t capacity);

// 编码：返回写入字节数；容量不足或数据非法返回 0。
size_t archery_record_encode(const archery_record_t *record,
                             uint8_t *buffer, size_t capacity);
// 解码：magic/版本/长度/CRC/环值逐项校验，任何一项不符返回 false。
// 兼容旧版（无靶型/无时间戳）记录，缺失字段按 40 半靶与"时间未知"处理。
bool archery_record_decode(archery_record_t *record,
                           const uint8_t *buffer, size_t length);
