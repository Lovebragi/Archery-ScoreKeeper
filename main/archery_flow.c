// main/archery_flow.c —— 页面/按键状态机实现（无 LVGL、无 ESP-IDF 依赖）。
#include "archery_flow.h"

#include <string.h>

// 三个物理键的语义（界面按用户视角布局，屏幕倒装由 BSP 处理）：
//   上/下短按  调整当前项（设置值、各环箭数、列表选择、确认选项）
//   确定短按  推进/提交/进入
//   确定长按  返回/取消（危险操作先弹确认页）

bool archery_flow_target_has_faces(uint8_t target) {
    return target == ARCHERY_TARGET_TRIPLE_40H;
}

// 把每组箭数默认平均分到上/中/下三个靶面，余数优先给上、再给中。
static void split_faces(uint8_t total, uint8_t faces[ARCHERY_FACES]) {
    const uint8_t base = (uint8_t)(total / ARCHERY_FACES);
    const uint8_t rest = (uint8_t)(total % ARCHERY_FACES);
    faces[0] = (uint8_t)(base + (rest > 0 ? 1u : 0u));
    faces[1] = (uint8_t)(base + (rest > 1 ? 1u : 0u));
    faces[2] = base;
}

uint8_t archery_flow_setup_faces_total(const archery_flow_t *flow) {
    uint8_t total = 0;
    for (uint8_t i = 0; i < ARCHERY_FACES; i++) {
        total = (uint8_t)(total + flow->setup_face[i]);
    }
    return total;
}

void archery_flow_init(archery_flow_t *flow) {
    memset(flow, 0, sizeof(*flow));
    flow->page = ARCHERY_PAGE_HOME;
    flow->abort_return = ARCHERY_PAGE_ENTRY;
    flow->setup_groups = ARCHERY_DEFAULT_GROUPS;
    flow->setup_arrows = ARCHERY_DEFAULT_ARROWS;
    flow->setup_row = 0;
    split_faces(flow->setup_arrows, flow->setup_face);
    flow->home_select = 0;
    flow->history_cursor = 0;
    flow->history_filter = ARCHERY_HISTORY_FILTER_ALL;
    flow->detail_group = 0;
    flow->detail_groups = 1;
    flow->entry_row = 0;
    flow->entry_face = 0;
    flow->settings_row = 0;
    flow->clock_fields.year = ARCHERY_CLOCK_EPOCH_YEAR;
    flow->clock_fields.month = 1;
    flow->clock_fields.day = 1;
    flow->clock_row = 0;
    flow->clock_set = false;
    archery_settings_defaults(&flow->settings);
}

void archery_flow_set_settings(archery_flow_t *flow, const archery_settings_t *settings) {
    if (!flow || !settings || !archery_settings_valid(settings)) return;
    flow->settings = *settings;
}

void archery_flow_set_history_count(archery_flow_t *flow, uint16_t count) {
    flow->history_count = count;
    // 空历史只有"返回主页"一行；否则记录后还有"清空"与"返回"两行。
    const uint16_t max_cursor = count == 0 ? 0 : (uint16_t)(count + 1u);
    if (flow->history_cursor > max_cursor) flow->history_cursor = max_cursor;
}

void archery_flow_set_clock(archery_flow_t *flow, uint32_t now, bool time_set) {
    if (!flow) return;
    flow->clock_now = now;
    flow->clock_set = time_set;
}

// 物理按键角色映射：自然方向就是"上/下/确定"；整机倒装时角色转 180°。
archery_key_t archery_flow_button_key(archery_button_t button, bool rotate_roles) {
    if (rotate_roles) {
        switch (button) {
        case ARCHERY_BUTTON_UP: return ARCHERY_KEY_OK;
        case ARCHERY_BUTTON_DOWN: return ARCHERY_KEY_DOWN;
        default: return ARCHERY_KEY_UP;
        }
    }
    switch (button) {
    case ARCHERY_BUTTON_UP: return ARCHERY_KEY_UP;
    case ARCHERY_BUTTON_DOWN: return ARCHERY_KEY_DOWN;
    default: return ARCHERY_KEY_OK;
    }
}

bool archery_flow_button_is_ok(archery_button_t button, bool rotate_roles) {
    return rotate_roles ? button == ARCHERY_BUTTON_UP : button == ARCHERY_BUTTON_OK;
}

static void move_cursor(uint8_t *value, int delta, uint8_t min, uint8_t max) {
    int next = (int)*value + delta;
    if (next < min) next = min;
    if (next > max) next = max;
    *value = (uint8_t)next;
}

static void move_cursor16(uint16_t *value, int delta, uint16_t min, uint16_t max) {
    int next = (int)*value + delta;
    if (next < (int)min) next = (int)min;
    if (next > (int)max) next = (int)max;
    *value = (uint16_t)next;
}

// 历史页行数：空历史只显示一行"返回主页"；有记录时是 记录 + 清空 + 返回。
static uint16_t history_rows(const archery_flow_t *flow) {
    return flow->history_count == 0 ? 1u : (uint16_t)(flow->history_count + 2u);
}

static uint16_t history_max_cursor(const archery_flow_t *flow) {
    return (uint16_t)(history_rows(flow) - 1u);
}

// 进入新一组的录入：各靶面各环箭数清零，光标回到最高环。
static void begin_group_input(archery_flow_t *flow) {
    memset(flow->counts, 0, sizeof(flow->counts));
    flow->entry_row = 0;
    flow->entry_face = 0;
}

// 三联靶某一靶面在本组里的起始箭位。
static uint8_t face_arrow_offset(const archery_flow_t *flow, uint8_t face) {
    uint8_t offset = 0;
    for (uint8_t i = 0; i < face; i++) offset = (uint8_t)(offset + flow->setup_face[i]);
    return offset;
}

static archery_act_t handle_home(archery_flow_t *flow, archery_key_t key) {
    switch (key) {
    case ARCHERY_KEY_UP:
        move_cursor(&flow->home_select, -1, 0, ARCHERY_HOME_ITEMS - 1u);
        break;
    case ARCHERY_KEY_DOWN:
        move_cursor(&flow->home_select, 1, 0, ARCHERY_HOME_ITEMS - 1u);
        break;
    case ARCHERY_KEY_OK:
        if (flow->home_select == 0) {
            flow->setup_row = 0;
            // 三联靶：进入设置页时确保上/中/下箭数与每组箭数一致。
            if (archery_flow_target_has_faces(flow->settings.target) &&
                archery_flow_setup_faces_total(flow) != flow->setup_arrows) {
                split_faces(flow->setup_arrows, flow->setup_face);
            }
            flow->page = ARCHERY_PAGE_SETUP;
        } else if (flow->home_select == 1) {
            flow->history_cursor = 0;
            flow->page = ARCHERY_PAGE_HISTORY;
        } else {
            flow->settings_row = 0;
            flow->page = ARCHERY_PAGE_SETTINGS;
        }
        break;
    case ARCHERY_KEY_OK_LONG:
        break;
    }
    return ARCHERY_ACT_NONE;
}

// 手动对时的字段调整：年/月/日会顺带把"日"钳到当月天数内。
static void clock_adjust(archery_datetime_t *dt, uint8_t field, int delta) {
    switch (field) {
    case 0: {
        int year = (int)dt->year + delta;
        if (year < (int)ARCHERY_CLOCK_MIN_YEAR) year = (int)ARCHERY_CLOCK_MIN_YEAR;
        if (year > (int)ARCHERY_CLOCK_MAX_YEAR) year = (int)ARCHERY_CLOCK_MAX_YEAR;
        dt->year = (uint16_t)year;
        break;
    }
    case 1: {
        int month = (int)dt->month + delta;
        if (month < 1) month = 1;
        if (month > 12) month = 12;
        dt->month = (uint8_t)month;
        break;
    }
    case 2: {
        int day = (int)dt->day + delta;
        const int max = (int)archery_clock_days_in_month(dt->year, dt->month);
        if (day < 1) day = 1;
        if (day > max) day = max;
        dt->day = (uint8_t)day;
        return;
    }
    case 3: {
        int hour = (int)dt->hour + delta;
        if (hour < 0) hour = 0;
        if (hour > 23) hour = 23;
        dt->hour = (uint8_t)hour;
        return;
    }
    default: {
        int minute = (int)dt->minute + delta;
        if (minute < 0) minute = 0;
        if (minute > 59) minute = 59;
        dt->minute = (uint8_t)minute;
        return;
    }
    }
    // 年/月变化后，把日钳进当月范围（例如 1/31 -> 2 月变成 2/28）。
    const int max = (int)archery_clock_days_in_month(dt->year, dt->month);
    if (dt->day > max) dt->day = (uint8_t)max;
}

static archery_act_t handle_clock(archery_flow_t *flow, archery_key_t key) {
    switch (key) {
    case ARCHERY_KEY_UP:
    case ARCHERY_KEY_DOWN:
        clock_adjust(&flow->clock_fields, flow->clock_row,
                     key == ARCHERY_KEY_UP ? 1 : -1);
        break;
    case ARCHERY_KEY_OK:
        if (flow->clock_row + 1u < ARCHERY_CLOCK_ROWS) {
            flow->clock_row++;
        } else {
            // 分字段按确定 = 保存对时并回到设置页的"返回"行。
            flow->page = ARCHERY_PAGE_SETTINGS;
            flow->settings_row = 5;
            return ARCHERY_ACT_CLOCK_SAVE;
        }
        break;
    case ARCHERY_KEY_OK_LONG:
        flow->page = ARCHERY_PAGE_SETTINGS;
        flow->settings_row = 5;
        return ARCHERY_ACT_CLOCK_SAVE;
    }
    return ARCHERY_ACT_NONE;
}

static archery_act_t handle_settings(archery_flow_t *flow, archery_key_t key) {
    switch (key) {
    case ARCHERY_KEY_UP:
    case ARCHERY_KEY_DOWN:
        if (flow->settings_row < 4 &&
            archery_settings_adjust(&flow->settings, flow->settings_row,
                                    key == ARCHERY_KEY_UP ? 1 : -1)) {
            // 靶型在下一组开始时生效；方向/亮度立即生效；息屏时间立即被空闲计时采用。
            return ARCHERY_ACT_SETTINGS_CHANGED;
        }
        break;
    case ARCHERY_KEY_OK:
        if (flow->settings_row == 4) {
            // 进入手动对时页：以当前时钟为初值。
            archery_seconds_to_datetime(flow->clock_now, &flow->clock_fields);
            flow->clock_row = 0;
            flow->page = ARCHERY_PAGE_CLOCK;
        } else if (flow->settings_row + 1u < ARCHERY_SETTINGS_ROWS) {
            flow->settings_row++;
        } else {
            flow->page = ARCHERY_PAGE_HOME;
            return ARCHERY_ACT_SETTINGS_SAVE;
        }
        break;
    case ARCHERY_KEY_OK_LONG:
        flow->page = ARCHERY_PAGE_HOME;
        return ARCHERY_ACT_SETTINGS_SAVE;
    }
    return ARCHERY_ACT_NONE;
}

static archery_act_t handle_setup(archery_flow_t *flow, archery_key_t key) {
    const bool faces = archery_flow_target_has_faces(flow->settings.target);
    const uint8_t start_row = faces ? 5u : 2u;
    switch (key) {
    case ARCHERY_KEY_UP:
    case ARCHERY_KEY_DOWN: {
        const int delta = key == ARCHERY_KEY_UP ? 1 : -1;
        if (flow->setup_row == 0) {
            flow->setup_groups = archery_groups_clamp((int)flow->setup_groups + delta);
        } else if (flow->setup_row == 1) {
            flow->setup_arrows = archery_arrows_clamp((int)flow->setup_arrows + delta);
            if (faces) split_faces(flow->setup_arrows, flow->setup_face);
        } else if (faces && flow->setup_row >= 2 && flow->setup_row <= 4) {
            const uint8_t face = (uint8_t)(flow->setup_row - 2u);
            move_cursor(&flow->setup_face[face], delta, 0, flow->setup_arrows);
        }
        break;
    }
    case ARCHERY_KEY_OK:
        if (flow->setup_row < start_row) {
            flow->setup_row++;
        } else if (!faces ||
                   archery_flow_setup_faces_total(flow) == flow->setup_arrows) {
            archery_session_begin(&flow->session, flow->setup_groups, flow->setup_arrows,
                                  flow->settings.target);
            begin_group_input(flow);
            flow->page = ARCHERY_PAGE_ENTRY;
            return ARCHERY_ACT_SESSION_START;
        }
        // 三联靶上/中/下合计与每组箭数不符时停在原地，由界面提示。
        break;
    case ARCHERY_KEY_OK_LONG:
        // 长按确定 = 返回上一项；在第一项上长按返回主页。
        if (flow->setup_row > 0) {
            flow->setup_row--;
        } else {
            flow->page = ARCHERY_PAGE_HOME;
        }
        break;
    }
    return ARCHERY_ACT_NONE;
}

static archery_act_t handle_entry(archery_flow_t *flow, archery_key_t key) {
    const bool faces = archery_flow_target_has_faces(flow->session.target);
    const uint8_t face = faces ? flow->entry_face : 0;
    const uint8_t slots = archery_target_slot_count(flow->session.target);
    const uint8_t face_arrows = faces ? flow->setup_face[face] : flow->session.arrows;
    uint8_t *counts = flow->counts[face];

    switch (key) {
    case ARCHERY_KEY_UP:
    case ARCHERY_KEY_DOWN:
        if (flow->entry_row < slots) {
            archery_counts_adjust(counts, flow->entry_row,
                                  key == ARCHERY_KEY_UP ? 1 : -1,
                                  face_arrows, flow->session.target);
        }
        break;
    case ARCHERY_KEY_OK:
        if (flow->entry_row < slots) {
            // 该靶面的箭数已经凑满：直接落到提交行。
            if (archery_counts_total(counts) == face_arrows) {
                flow->entry_row = slots;
            } else {
                flow->entry_row++;
            }
        } else if (archery_counts_total(counts) == face_arrows &&
                   archery_session_apply_face_counts(
                       &flow->session, face_arrow_offset(flow, face), face_arrows,
                       counts)) {
            // 三联靶先切到下一靶面；最后一个靶面提交整组。
            if (faces && face + 1u < ARCHERY_FACES) {
                flow->entry_face = (uint8_t)(face + 1u);
                flow->entry_row = 0;
            } else {
                archery_session_commit_group(&flow->session);
                flow->page = ARCHERY_PAGE_SUMMARY;
            }
        } else {
            // 还没记满：回到该靶面第一行继续补。
            flow->entry_row = 0;
        }
        break;
    case ARCHERY_KEY_OK_LONG:
        // 长按确定 = 返回上一项；三联靶在第一行长按会退回上一靶的提交行。
        if (flow->entry_row > 0) {
            flow->entry_row--;
        } else if (faces && flow->entry_face > 0) {
            flow->entry_face--;
            flow->entry_row = slots;
        } else {
            flow->abort_return = ARCHERY_PAGE_ENTRY;
            flow->confirm_yes = false;
            flow->page = ARCHERY_PAGE_ABORT_CONFIRM;
        }
        break;
    }
    return ARCHERY_ACT_NONE;
}

static archery_act_t handle_summary(archery_flow_t *flow, archery_key_t key) {
    switch (key) {
    case ARCHERY_KEY_OK:
        if (archery_session_complete(&flow->session)) {
            flow->save_failed = false;
            flow->page = ARCHERY_PAGE_FINISH;
            return ARCHERY_ACT_SAVE_RECORD;
        }
        begin_group_input(flow);
        flow->page = ARCHERY_PAGE_ENTRY;
        break;
    case ARCHERY_KEY_UP:
    case ARCHERY_KEY_DOWN:
        break;
    case ARCHERY_KEY_OK_LONG:
        flow->abort_return = ARCHERY_PAGE_SUMMARY;
        flow->confirm_yes = false;
        flow->page = ARCHERY_PAGE_ABORT_CONFIRM;
        break;
    }
    return ARCHERY_ACT_NONE;
}

static archery_act_t handle_history(archery_flow_t *flow, archery_key_t key) {
    switch (key) {
    case ARCHERY_KEY_UP:
    case ARCHERY_KEY_DOWN: {
        const int delta = key == ARCHERY_KEY_UP ? -1 : 1;
        const uint16_t max = history_max_cursor(flow);
        move_cursor16(&flow->history_cursor, delta, 0, max);
        break;
    }
    case ARCHERY_KEY_OK:
        if (flow->history_count == 0) {
            // 空历史的唯一一行是"返回主页"。
            flow->page = ARCHERY_PAGE_HOME;
        } else if (flow->history_cursor < flow->history_count) {
            flow->detail_group = 0;
            flow->page = ARCHERY_PAGE_DETAIL;
        } else if (flow->history_cursor == flow->history_count) {
            // "清空全部记录"行
            flow->confirm_yes = false;
            flow->page = ARCHERY_PAGE_CLEAR_CONFIRM;
        } else {
            // "返回主页"行
            flow->page = ARCHERY_PAGE_HOME;
        }
        break;
    case ARCHERY_KEY_OK_LONG:
        // 长按确定循环切换筛选：全部 -> 今天 -> 最近 7 天 -> 全部。
        flow->history_filter = (uint8_t)((flow->history_filter + 1u) %
                                         ARCHERY_HISTORY_FILTER_COUNT);
        flow->history_cursor = 0;
        return ARCHERY_ACT_HISTORY_FILTER;
    }
    return ARCHERY_ACT_NONE;
}

static archery_act_t handle_detail(archery_flow_t *flow, archery_key_t key) {
    const uint8_t max_group =
        flow->detail_groups == 0 ? 0 : (uint8_t)(flow->detail_groups - 1u);
    switch (key) {
    case ARCHERY_KEY_UP:
        if (flow->detail_group > 0) flow->detail_group--;
        break;
    case ARCHERY_KEY_DOWN:
        if (flow->detail_group < max_group) flow->detail_group++;
        break;
    case ARCHERY_KEY_OK:
    case ARCHERY_KEY_OK_LONG:
        flow->page = ARCHERY_PAGE_HISTORY;
        break;
    }
    return ARCHERY_ACT_NONE;
}

static archery_act_t handle_clear_confirm(archery_flow_t *flow, archery_key_t key) {
    switch (key) {
    case ARCHERY_KEY_UP:
    case ARCHERY_KEY_DOWN:
        flow->confirm_yes = !flow->confirm_yes;
        break;
    case ARCHERY_KEY_OK:
        if (flow->confirm_yes) {
            flow->page = ARCHERY_PAGE_HISTORY;
            flow->history_cursor = 0;
            flow->history_count = 0;
            return ARCHERY_ACT_CLEAR_HISTORY;
        }
        flow->page = ARCHERY_PAGE_HISTORY;
        break;
    case ARCHERY_KEY_OK_LONG:
        flow->page = ARCHERY_PAGE_HISTORY;
        break;
    }
    return ARCHERY_ACT_NONE;
}

static archery_act_t handle_abort_confirm(archery_flow_t *flow, archery_key_t key) {
    switch (key) {
    case ARCHERY_KEY_UP:
    case ARCHERY_KEY_DOWN:
        flow->confirm_yes = !flow->confirm_yes;
        break;
    case ARCHERY_KEY_OK:
        if (flow->confirm_yes) {
            // 放弃本次训练：回到主页，未提交的成绩不写历史。
            flow->page = ARCHERY_PAGE_HOME;
        } else {
            flow->page = flow->abort_return;
        }
        break;
    case ARCHERY_KEY_OK_LONG:
        flow->page = flow->abort_return;
        break;
    }
    return ARCHERY_ACT_NONE;
}

archery_act_t archery_flow_handle(archery_flow_t *flow, archery_key_t key) {
    switch (flow->page) {
    case ARCHERY_PAGE_HOME: return handle_home(flow, key);
    case ARCHERY_PAGE_SETUP: return handle_setup(flow, key);
    case ARCHERY_PAGE_ENTRY: return handle_entry(flow, key);
    case ARCHERY_PAGE_SUMMARY: return handle_summary(flow, key);
    case ARCHERY_PAGE_FINISH:
        // 完成页：任意确定回到主页。
        if (key == ARCHERY_KEY_OK || key == ARCHERY_KEY_OK_LONG) {
            flow->page = ARCHERY_PAGE_HOME;
        }
        return ARCHERY_ACT_NONE;
    case ARCHERY_PAGE_HISTORY: return handle_history(flow, key);
    case ARCHERY_PAGE_DETAIL: return handle_detail(flow, key);
    case ARCHERY_PAGE_CLEAR_CONFIRM: return handle_clear_confirm(flow, key);
    case ARCHERY_PAGE_ABORT_CONFIRM: return handle_abort_confirm(flow, key);
    case ARCHERY_PAGE_SETTINGS: return handle_settings(flow, key);
    case ARCHERY_PAGE_CLOCK: return handle_clock(flow, key);
    }
    return ARCHERY_ACT_NONE;
}
