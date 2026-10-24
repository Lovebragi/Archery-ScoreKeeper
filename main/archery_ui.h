// main/archery_ui.h —— 应用界面渲染入口（必须在 LVGL 锁内调用）。
#pragma once

#include "archery_flow.h"
#include "archery_store.h"

// 按 flow 当前页面重建整屏。数字类页面数据（历史记录）来自 history。
// battery_soc < 0 表示电量不可用，此时只画空心电池图标。
void archery_ui_show(const archery_flow_t *flow, const archery_history_t *history,
                     int battery_soc);

// 删除当前应用界面（退出应用时调用）。
void archery_ui_clear(void);
