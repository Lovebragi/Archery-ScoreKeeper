// main/archery_app.c —— 把 BSP、纯状态机、NVS 与界面装配成应用。
//
// 线程模型：
//   * button 组件的回调（共享 esp_timer 任务）只把按键事件放进队列；
//   * 唯一的 archery 任务消费队列，在其中更新 flow、读写 NVS 并重建 LVGL 页面
//     （重建前获取 bsp_lvgl_lock）；
//   * 队列空闲 500ms 时刷新电量；超过设置的空闲时间后息屏并进入浅睡，
//     任意键唤醒并回到原来的页面。
#include "archery_app.h"

#include "archery_flow.h"
#include "archery_history.h"
#include "archery_score.h"
#include "archery_settings.h"
#include "archery_store.h"
#include "archery_ui.h"
#include "archery_version.h"
#include "archery_voice.h"
#include "archery_wifi.h"
#include "archery_wifi_view.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "key_guard.h"
#include <string.h>

static const char *TAG = "archery_app";

#define KEY_QUEUE_DEPTH 8
#define BATTERY_POLL_MS 10000
#define WORKER_STACK_BYTES 6144
// 息屏浅睡后，唤醒键还按着时先吞掉按键动作，直到松手（最长兜底 5 秒）。
#define WAKE_GUARD_MAX_US (5LL * 1000 * 1000)

typedef enum {
    APP_MSG_KEY = 0,
    APP_MSG_WIFI,
    APP_MSG_VOICE_START,
    APP_MSG_VOICE_STOP,
} app_msg_type_t;

typedef struct {
    uint8_t type;
    archery_key_t key;
} app_message_t;

static QueueHandle_t s_queue;
static TaskHandle_t s_worker;
static archery_flow_t s_flow;
static archery_history_t s_history;
static int s_battery_soc = -1;
static int64_t s_session_start_us;
static int64_t s_last_input_us;
static int64_t s_next_battery_us;
static volatile bool s_ready;
// 按键角色是否随屏幕 180° 倒转。按钮任务读取，设置页修改，用独立的 volatile
// 变量避免和 flow 结构体的其它字段一起被读到中间状态。
static volatile bool s_rotate_keys = (ARCHERY_SETTINGS_DEFAULT_ROTATE != 0);
// 按键唤醒后，那颗键在整个启动期间还按着；在它松手前丢弃按键动作，
// 避免把"唤醒键仍在按"误判成长按。
static key_guard_t s_key_guard;
// 墙上时钟：基准时间 + 单调计时器。设备没有 RTC 电池，断电后时间从上次保存
// 的时刻继续；设置页可手动校准。
static uint32_t s_clock_base;
static int64_t s_clock_base_us;
static bool s_clock_set;
static uint32_t s_clock_saved;
static int64_t s_next_clock_save_us;
static uint32_t s_session_when;
static archery_wifi_view_t s_wifi_view;
// 密码页长按上/下连发：方向、下一次触发时间、是否吞掉松开时的单击。
static uint8_t s_pw_dir;
static int64_t s_pw_next_step;
static bool s_pw_swallow_click;

// Wi-Fi 任务发来的通知：只入队，界面在主任务里刷新。
static void on_wifi_event(void *user) {
    (void)user;
    if (!s_queue) return;
    const app_message_t message = {.type = APP_MSG_WIFI};
    (void)xQueueSend(s_queue, &message, 0);
}

static uint32_t clock_now(void) {
    const int64_t elapsed = esp_timer_get_time() - s_clock_base_us;
    if (elapsed <= 0) return s_clock_base;
    return s_clock_base + (uint32_t)(elapsed / 1000000);
}

static void clock_set_to(uint32_t seconds) {
    s_clock_base = seconds;
    s_clock_base_us = esp_timer_get_time();
    s_clock_set = seconds != 0;
}

// 首次开机没有保存过时间时，用固件编译时间做近似种子（比 1970 年更接近真实）。
static uint32_t clock_build_seed(void) {
    archery_datetime_t dt;
    if (!archery_clock_parse_build(__DATE__, __TIME__, &dt)) return 0;
    return archery_datetime_to_seconds(&dt);
}

static void clock_persist(bool force) {
    const uint32_t now = clock_now();
    if (!force && now <= s_clock_saved + 60u) return;
    if (archery_store_clock_save(now, s_clock_set) == ESP_OK) {
        s_clock_saved = now;
    }
}

// 把 Wi-Fi 视图同步到 flow（扫描条数、加密网络与配网状态）。
static void sync_wifi_flow(void) {
    archery_wifi_fill_view(&s_wifi_view);
    archery_flow_set_wifi_scan(&s_flow, s_wifi_view.ap_count);
    uint8_t mask = 0;
    for (uint8_t i = 0; i < s_wifi_view.ap_count && i < 8; i++) {
        if (s_wifi_view.secure[i]) mask = (uint8_t)(mask | (1u << i));
    }
    archery_flow_set_wifi_security(&s_flow, mask);
    archery_flow_set_wifi_provisioning(&s_flow, archery_wifi_provisioning());
}

static void redraw(void) {
    archery_flow_set_clock(&s_flow, clock_now(), s_clock_set);
    sync_wifi_flow();
    archery_ui_set_wifi_view(&s_wifi_view);
    archery_ui_set_voice_status(archery_voice_status());
    if (!bsp_lvgl_lock(500)) {
        ESP_LOGW(TAG, "获取 LVGL 锁超时，跳过本次重绘");
        return;
    }
    archery_ui_show(&s_flow, &s_history, s_battery_soc);
    bsp_lvgl_unlock();
}

static void refresh_battery(void) {
    const int soc = bsp_battery_soc();
    if (soc == s_battery_soc) return;
    s_battery_soc = soc;
    redraw();
}

static void sync_detail_group(void) {
    if (s_flow.page != ARCHERY_PAGE_DETAIL) return;
    const archery_record_t *record = archery_history_visible_record(
        &s_history, s_flow.history_filter, clock_now(), s_flow.history_cursor);
    s_flow.detail_groups = record ? record->groups : 1;
    if (s_flow.detail_group >= s_flow.detail_groups) {
        s_flow.detail_group = (uint8_t)(s_flow.detail_groups - 1u);
    }
}

// 按当前筛选重新统计可见条数，并让 flow 的光标保持在范围内。
static void refresh_history_count(void) {
    const uint16_t count =
        archery_history_visible_count(&s_history, s_flow.history_filter, clock_now());
    archery_flow_set_history_count(&s_flow, count);
}

static void apply_settings(const archery_settings_t *settings) {
    // 画面方向与亮度立即生效；息屏时间由 worker 的空闲判断直接读取 flow 里的副本。
    // 改 MADCTL 时先拿 LVGL 锁，避免和正在进行的刷屏并发。
    s_rotate_keys = settings->rotate != 0;
    if (bsp_lvgl_lock(200)) {
        bsp_display_backlight(settings->brightness);
        (void)bsp_display_set_flip_180(settings->rotate != 0);
        bsp_lvgl_unlock();
    }
}

// Wi-Fi 任务的事件：刷新视图、同步扫描条数，若拿到 NTP 时间就更新时钟。
// 密码页：按住上/下时自动连发，避免 62 个字符一个个点。
static void password_repeat_poll(void) {
    if (s_flow.page != ARCHERY_PAGE_WIFI_PASSWORD) {
        s_pw_dir = 0;
        s_pw_swallow_click = false;
        return;
    }
    // 先按电压判断"物理键"，再套用屏幕方向对应的角色旋转：
    // 倒装时物理上键是逻辑确定、物理确定键才是逻辑上。
    const int mv = bsp_button_read_mv();
    archery_button_t physical = ARCHERY_BUTTON_OK;
    bool pressed = false;
    if (mv >= 0 && mv < 150) {
        physical = ARCHERY_BUTTON_UP;
        pressed = true;
    } else if (mv >= 150 && mv < 447) {
        physical = ARCHERY_BUTTON_DOWN;
        pressed = true;
    } else if (mv >= 447 && mv < BSP_BTN_MV_RELEASED_MIN) {
        physical = ARCHERY_BUTTON_OK;
        pressed = true;
    }
    uint8_t dir = 0;
    if (pressed) {
        const archery_key_t logical = archery_flow_button_key(physical, s_rotate_keys);
        if (logical == ARCHERY_KEY_UP) {
            dir = 1;
        } else if (logical == ARCHERY_KEY_DOWN) {
            dir = 2;
        }
    }
    const int64_t now = esp_timer_get_time();
    if (dir == 0) {
        s_pw_dir = 0;
        return;
    }
    if (dir != s_pw_dir) {
        s_pw_dir = dir;
        s_pw_next_step = now + 350000;  // 按住 350ms 后开始连发
        s_pw_swallow_click = true;
        (void)archery_flow_handle(&s_flow,
                                  dir == 1 ? ARCHERY_KEY_UP : ARCHERY_KEY_DOWN);
        redraw();
        return;
    }
    if (now >= s_pw_next_step) {
        (void)archery_flow_handle(&s_flow,
                                  dir == 1 ? ARCHERY_KEY_UP : ARCHERY_KEY_DOWN);
        s_pw_next_step = now + 60000;  // 约 16 次/秒
        redraw();
    }
}

static void handle_wifi_event(void) {
    sync_wifi_flow();
    uint32_t seconds = 0;
    if (archery_wifi_take_time(&seconds)) {
        clock_set_to(seconds);
        clock_persist(true);
    }
    redraw();
}

static void handle_key(archery_key_t key) {
    const archery_act_t action = archery_flow_handle(&s_flow, key);
    switch (action) {
    case ARCHERY_ACT_SESSION_START:
        s_session_start_us = esp_timer_get_time();
        s_session_when = clock_now();
        break;
    case ARCHERY_ACT_SAVE_RECORD: {
        const int64_t elapsed_us = esp_timer_get_time() - s_session_start_us;
        uint32_t seconds = elapsed_us > 0 ? (uint32_t)(elapsed_us / 1000000) : 0u;
        if (seconds > UINT16_MAX) seconds = UINT16_MAX;
        archery_record_t record;
        archery_record_from_session(&record, &s_flow.session, 0, (uint16_t)seconds,
                                    s_session_when);
        if (archery_store_save(&s_history, &record) != ESP_OK) {
            s_flow.save_failed = true;
            ESP_LOGE(TAG, "训练记录写入失败");
        }
        clock_persist(false);
        refresh_history_count();
        break;
    }
    case ARCHERY_ACT_CLEAR_HISTORY:
        if (archery_store_clear(&s_history) != ESP_OK) {
            ESP_LOGE(TAG, "清空历史失败，保留原有记录");
        }
        refresh_history_count();
        break;
    case ARCHERY_ACT_SETTINGS_CHANGED:
        apply_settings(&s_flow.settings);
        break;
    case ARCHERY_ACT_SETTINGS_SAVE:
        apply_settings(&s_flow.settings);
        if (archery_settings_save(&s_flow.settings) != ESP_OK) {
            ESP_LOGE(TAG, "设置保存失败");
        }
        break;
    case ARCHERY_ACT_CLOCK_SAVE: {
        const uint32_t seconds = archery_datetime_to_seconds(&s_flow.clock_fields);
        if (seconds != 0) {
            clock_set_to(seconds);
            clock_persist(true);
            archery_flow_set_clock(&s_flow, seconds, true);
        }
        break;
    }
    case ARCHERY_ACT_HISTORY_FILTER:
        refresh_history_count();
        break;

    case ARCHERY_ACT_WIFI_ENTER:
        archery_wifi_fill_view(&s_wifi_view);
        archery_flow_set_wifi_scan(&s_flow, s_wifi_view.ap_count);
        break;
    case ARCHERY_ACT_WIFI_SCAN:
        (void)archery_wifi_scan();
        break;
    case ARCHERY_ACT_WIFI_CONNECT:
        (void)archery_wifi_connect_index(s_flow.wifi_row);
        break;
    case ARCHERY_ACT_WIFI_CONNECT_PASSWORD: {
        const uint8_t index = s_flow.wifi_target;
        if (index < ARCHERY_WIFI_AP_MAX) {
            char ssid[ARCHERY_WIFI_SSID_MAX];
            strncpy(ssid, s_wifi_view.ssid[index], sizeof(ssid) - 1);
            ssid[sizeof(ssid) - 1] = '\0';
            (void)archery_wifi_connect(ssid, s_flow.wifi_password);
        }
        break;
    }
    case ARCHERY_ACT_WIFI_PROVISION:
        // flow 依据 wifi_provisioning 选择开或停；这里再查一次运行态，
        // 避免事件回调尚未同步时把"停止"又变成一次幂等的 start。
        if (archery_wifi_provisioning()) {
            (void)archery_wifi_stop_provisioning();
        } else {
            (void)archery_wifi_start_provisioning(NULL);
        }
        break;
    case ARCHERY_ACT_WIFI_SYNC_TIME:
        (void)archery_wifi_sync_time();
        break;
    case ARCHERY_ACT_WIFI_STOP:
        if (archery_wifi_provisioning()) {
            (void)archery_wifi_stop_provisioning();
        } else {
            (void)archery_wifi_disconnect();
        }
        break;
    case ARCHERY_ACT_WIFI_LEAVE:
        (void)archery_wifi_stop_provisioning();
        break;
    case ARCHERY_ACT_NONE:
        break;
    }
    sync_detail_group();
    redraw();
}

// 物理按键 → 逻辑键。屏幕倒装时按 180° 旋转整列按键的角色。
static archery_button_t physical_button(bsp_btn_t btn) {
    switch (btn) {
    case BSP_BTN_UP: return ARCHERY_BUTTON_UP;
    case BSP_BTN_DOWN: return ARCHERY_BUTTON_DOWN;
    default: return ARCHERY_BUTTON_OK;
    }
}

// button 回调运行在共享 esp_timer 任务：只做映射与入队，不碰 LVGL、不阻塞。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t event, void *user) {
    (void)user;
    // 先消费唤醒守卫再判断就绪：松开事件可能早于队列就绪到达，若在这里被
    // 提前返回丢掉，守卫会一直拦到兜底时限。
    const bool was_armed = s_key_guard.armed;
    if (key_guard_consume(&s_key_guard, event == BSP_BTN_RELEASE, esp_timer_get_time())) {
        if (was_armed && !s_key_guard.armed) {
            ESP_LOGI(TAG, "唤醒键已松手，按键恢复响应");
        }
        return;
    }
    const archery_button_t physical = physical_button(btn);
    if (!s_ready || !s_queue) return;

    // PTT：上键长按开始，松手结束。松手事件只用于结束录音，不产生普通按键。
    if (event == BSP_BTN_RELEASE) {
        if (archery_voice_active()) {
            const app_message_t stop = {.type = APP_MSG_VOICE_STOP};
            (void)xQueueSend(s_queue, &stop, 0);
        }
        return;
    }

    archery_key_t key;
    if (event == BSP_BTN_LONG) {
        if (archery_flow_button_is_ok(physical, s_rotate_keys)) {
            key = ARCHERY_KEY_OK_LONG;
        } else if (archery_flow_button_key(physical, s_rotate_keys) == ARCHERY_KEY_UP) {
            const app_message_t start = {.type = APP_MSG_VOICE_START};
            (void)xQueueSend(s_queue, &start, 0);
            return;
        } else {
            return;
        }
    } else if (event == BSP_BTN_CLICK || event == BSP_BTN_DOUBLE) {
        // 双击窗口内的快速连按会被驱动合并成 DOUBLE；本应用没有双击语义，
        // 按一次单击处理，避免快速调整数值时"按键失灵"。
        key = archery_flow_button_key(physical, s_rotate_keys);
    } else {
        return;
    }
    const app_message_t message = {.type = APP_MSG_KEY, .key = key};
    (void)xQueueSend(s_queue, &message, 0);
}

// 息屏 + 浅睡；任意键唤醒。返回后页面与训练状态都还在（RAM 不掉电）。
// 全程持有 LVGL 锁：确保没有正在进行的刷屏，唤醒后再重绘一帧。
static void light_sleep_now(void) {
    s_last_input_us = esp_timer_get_time();

    // 有键按着时不要睡：低电平唤醒条件会在入睡瞬间成立，导致立刻醒。
    const int mv = bsp_button_read_mv();
    if (mv < BSP_BTN_MV_RELEASED_MIN) {
        if (mv < 0) ESP_LOGW(TAG, "按键电压读取失败，本次不休眠");
        return;
    }
    if (!bsp_lvgl_lock(200)) return;

    app_message_t pending;
    while (xQueueReceive(s_queue, &pending, 0) == pdTRUE) {
    }

    bsp_display_backlight(0);
    const esp_err_t display_err = bsp_display_set_power(false);
    esp_err_t wake_err = gpio_wakeup_enable((gpio_num_t)BSP_BTN_GPIO, GPIO_INTR_LOW_LEVEL);
    if (wake_err == ESP_OK) wake_err = esp_sleep_enable_gpio_wakeup();
    int level = 1;
    const esp_err_t suspend_err = bsp_button_suspend_for_light_sleep(&level);

    if (display_err != ESP_OK || wake_err != ESP_OK || suspend_err != ESP_OK) {
        // 回滚到可操作状态：重新打开显示、恢复按键。
        (void)gpio_wakeup_disable((gpio_num_t)BSP_BTN_GPIO);
        (void)bsp_display_set_power(true);
        bsp_display_backlight(s_flow.settings.brightness);
        if (suspend_err == ESP_OK) {
            (void)bsp_button_resume_after_light_sleep(on_key, NULL);
        }
        bsp_lvgl_unlock();
        ESP_LOGE(TAG, "准备浅睡失败(display=%s wake=%s button=%s)，保持唤醒",
                 esp_err_to_name(display_err), esp_err_to_name(wake_err),
                 esp_err_to_name(suspend_err));
        return;
    }

    ESP_LOGI(TAG, "息屏浅睡，任意键唤醒");
    const esp_err_t sleep_err = esp_light_sleep_start();
    const uint32_t causes = esp_sleep_get_wakeup_causes();

    // ---- 唤醒 ----
    (void)gpio_wakeup_disable((gpio_num_t)BSP_BTN_GPIO);
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    (void)bsp_display_set_power(true);
    bsp_display_backlight(s_flow.settings.brightness);
    // 唤醒那一下的按键还在按着：先吞掉，直到松手（或兜底时限）。
    key_guard_arm(&s_key_guard, esp_timer_get_time(), WAKE_GUARD_MAX_US);
    const esp_err_t resume_err = bsp_button_resume_after_light_sleep(on_key, NULL);
    const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
    key_guard_sync_released(&s_key_guard, released);
    while (xQueueReceive(s_queue, &pending, 0) == pdTRUE) {
    }
    archery_ui_show(&s_flow, &s_history, s_battery_soc);
    bsp_lvgl_unlock();

    s_last_input_us = esp_timer_get_time();
    s_next_battery_us = 0;  // 醒来后立刻刷新电量
    ESP_LOGI(TAG, "已唤醒(sleep=%s resume=%s causes=0x%02x)",
             esp_err_to_name(sleep_err), esp_err_to_name(resume_err),
             (unsigned)causes);
}

// 把语音识别结果写入当前靶面的录入计数；只接受正好凑满本组箭数的结果。
static void apply_voice_result(const archery_voice_result_t *result) {
    const char *failure = NULL;
    if (!result->ok) {
        failure = "没听清，请重说";
    } else if (s_flow.page != ARCHERY_PAGE_ENTRY) {
        failure = "已忽略（不在录入页）";
    }

    const uint8_t target = s_flow.session.target;
    const bool faces = archery_flow_target_has_faces(target);
    const uint8_t face = faces ? s_flow.entry_face : 0;
    const uint8_t slots = archery_target_slot_count(target);
    const uint8_t face_arrows = faces ? s_flow.setup_face[face] : s_flow.session.arrows;
    uint8_t counts[ARCHERY_RING_SLOT_MAX];
    memset(counts, 0, sizeof(counts));

    uint16_t total = 0;
    bool has_all = false;
    uint8_t all_slot = 0xFF;

    if (!failure) {
        for (uint8_t i = 0; i < result->count; i++) {
            const archery_voice_item_t *item = &result->items[i];
            uint8_t slot = 0xFF;
            for (uint8_t s = 0; s < slots; s++) {
                if (archery_target_slot_code(target, s) == item->ring) {
                    slot = s;
                    break;
                }
            }
            if (slot == 0xFF) {
                failure = "环值不在本靶型";
                break;
            }
            if (item->all) {
                if (has_all) {
                    failure = "重复的全量项";
                    break;
                }
                has_all = true;
                all_slot = slot;
                continue;
            }
            counts[slot] = (uint8_t)(counts[slot] + item->count);
            total += item->count;
        }
    }
    if (!failure && has_all) {
        if (total >= face_arrows) {
            failure = "数量超出本组";
        } else {
            counts[all_slot] = (uint8_t)(face_arrows - total);
            total = face_arrows;
        }
    }
    if (!failure && total != face_arrows) {
        failure = "数量对不上，请重说";
    }

    if (failure) {
        archery_voice_set_status(failure);
    } else {
        memcpy(s_flow.counts[face], counts, slots);
        const uint8_t first = archery_target_entry_first_slot(target,
                                                             s_flow.settings.record_x);
        s_flow.entry_row = (uint8_t)(slots - first);
        archery_voice_set_status("已填入，确定提交");
    }
    redraw();
}

static void worker_task(void *argument) {
    (void)argument;
    s_next_battery_us = esp_timer_get_time();
    for (;;) {
        archery_voice_result_t voice;
        if (archery_voice_take_result(&voice)) {
            apply_voice_result(&voice);
        }
        app_message_t message;
        TickType_t wait = archery_voice_active() ? pdMS_TO_TICKS(100) : pdMS_TO_TICKS(500);
        if (s_flow.page == ARCHERY_PAGE_WIFI_PASSWORD) wait = pdMS_TO_TICKS(60);
        if (xQueueReceive(s_queue, &message, wait) == pdTRUE) {
            s_last_input_us = esp_timer_get_time();
            if (message.type == APP_MSG_KEY) {
                if (s_flow.page == ARCHERY_PAGE_WIFI_PASSWORD &&
                    (message.key == ARCHERY_KEY_UP || message.key == ARCHERY_KEY_DOWN) &&
                    s_pw_swallow_click) {
                    s_pw_swallow_click = false;  // 本次按下已由连发处理
                } else {
                    handle_key(message.key);
                }
            } else if (message.type == APP_MSG_VOICE_START) {
                if (s_flow.page == ARCHERY_PAGE_ENTRY && archery_voice_enabled() &&
                    !archery_voice_active()) {
                    const bool faces = archery_flow_target_has_faces(s_flow.session.target);
                    const uint8_t face = faces ? s_flow.entry_face : 0;
                    archery_voice_set_context(s_flow.session.target,
                                              faces ? s_flow.setup_face[face]
                                                    : s_flow.session.arrows);
                    archery_voice_set_status("录音中，松手结束");
                    redraw();
                    (void)archery_voice_start();
                }
            } else if (message.type == APP_MSG_VOICE_STOP) {
                archery_voice_stop();
            } else {
                handle_wifi_event();
            }
            continue;
        }
        password_repeat_poll();
        const int64_t now = esp_timer_get_time();
        if (now >= s_next_battery_us) {
            refresh_battery();
            s_next_battery_us = now + (int64_t)BATTERY_POLL_MS * 1000;
        }
        if (now >= s_next_clock_save_us) {
            // 每 5 分钟把时钟基准写一次 NVS；写入内容很小，磨损可忽略。
            clock_persist(false);
            s_next_clock_save_us = now + (int64_t)300 * 1000000;
        }
        const uint16_t off_seconds = s_flow.settings.off_seconds;
        if (off_seconds > 0 &&
            now - s_last_input_us >= (int64_t)off_seconds * 1000000 &&
            s_flow.page != ARCHERY_PAGE_WIFI &&
            s_flow.page != ARCHERY_PAGE_WIFI_PASSWORD &&
            !archery_wifi_provisioning()) {
            light_sleep_now();
        }
    }
}

void archery_app_start(void) {
    if (esp_sleep_get_wakeup_causes() & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), WAKE_GUARD_MAX_US);
        ESP_LOGI(TAG, "按键唤醒：先屏蔽按键动作，直到该键松手");
    }

    archery_flow_init(&s_flow);
    ESP_LOGI(TAG, "%s %s", ARCHERY_FIRMWARE_CHANNEL, ARCHERY_FIRMWARE_VERSION);

    if (archery_store_load(&s_history) != ESP_OK) {
        ESP_LOGW(TAG, "历史记录载入失败，从空历史开始");
        memset(&s_history, 0, sizeof(s_history));
        archery_history_meta_init(&s_history.meta);
    }

    archery_wifi_set_history(&s_history);

    archery_settings_t stored;
    if (archery_settings_load(&stored) != ESP_OK) {
        ESP_LOGW(TAG, "设置读取失败，使用默认亮度/息屏时间");
    }
    archery_flow_set_settings(&s_flow, &stored);
    apply_settings(&s_flow.settings);

    // 时钟：优先用上次保存的时间；没有就用固件编译时间近似。
    uint32_t saved_clock = 0;
    bool saved_set = false;
    if (archery_store_clock_load(&saved_clock, &saved_set) != ESP_OK || saved_clock == 0) {
        saved_clock = clock_build_seed();
        saved_set = false;
    }
    s_clock_base = saved_clock;
    s_clock_base_us = esp_timer_get_time();
    s_clock_set = saved_set;
    s_clock_saved = saved_clock;
    s_next_clock_save_us = s_clock_base_us + (int64_t)300 * 1000000;
    archery_flow_set_clock(&s_flow, clock_now(), s_clock_set);

    s_battery_soc = bsp_battery_soc();
    s_session_start_us = esp_timer_get_time();
    s_last_input_us = s_session_start_us;
    refresh_history_count();

    if (archery_wifi_init(on_wifi_event, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi 初始化失败，联网页不可用");
    }
    archery_wifi_fill_view(&s_wifi_view);
    archery_flow_set_wifi_scan(&s_flow, s_wifi_view.ap_count);

    if (archery_voice_init() != ESP_OK) {
        ESP_LOGW(TAG, "语音模块初始化失败，语音报靶不可用");
    }

    redraw();

    s_queue = xQueueCreate(KEY_QUEUE_DEPTH, sizeof(app_message_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "按键队列创建失败，界面只读");
        return;
    }
    if (xTaskCreate(worker_task, "archery", WORKER_STACK_BYTES, NULL, 5, &s_worker) != pdPASS) {
        ESP_LOGE(TAG, "应用任务创建失败，界面只读");
        vQueueDelete(s_queue);
        s_queue = NULL;
        return;
    }
    s_ready = true;

    const esp_err_t err = bsp_button_init(on_key, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(err));
        s_ready = false;
    } else if (s_key_guard.armed) {
        // 唤醒键若在按键驱动启动前就已松手，组件不会产生 release 事件；
        // 初始化完成后按真实电压同步一次，已松手立即撤防。
        const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
        key_guard_sync_released(&s_key_guard, released);
        if (!s_key_guard.armed) ESP_LOGI(TAG, "唤醒键已提前松手，撤防");
    }
    ESP_LOGI(TAG, "就绪：历史 %u 条，电量 %d，亮度 %u%%，息屏 %u 秒",
             (unsigned)s_history.meta.count, s_battery_soc,
             (unsigned)s_flow.settings.brightness,
             (unsigned)s_flow.settings.off_seconds);
}
