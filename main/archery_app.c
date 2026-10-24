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
#include "archery_settings.h"
#include "archery_store.h"
#include "archery_ui.h"
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

typedef struct {
    archery_key_t key;
} key_message_t;

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

static void redraw(void) {
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
    const archery_record_t *record =
        archery_history_record(&s_history, s_flow.history_cursor);
    s_flow.detail_groups = record ? record->groups : 1;
    if (s_flow.detail_group >= s_flow.detail_groups) {
        s_flow.detail_group = (uint8_t)(s_flow.detail_groups - 1u);
    }
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

static void handle_key(archery_key_t key) {
    const archery_act_t action = archery_flow_handle(&s_flow, key);
    switch (action) {
    case ARCHERY_ACT_SESSION_START:
        s_session_start_us = esp_timer_get_time();
        break;
    case ARCHERY_ACT_SAVE_RECORD: {
        const int64_t elapsed_us = esp_timer_get_time() - s_session_start_us;
        uint32_t seconds = elapsed_us > 0 ? (uint32_t)(elapsed_us / 1000000) : 0u;
        if (seconds > UINT16_MAX) seconds = UINT16_MAX;
        archery_record_t record;
        archery_record_from_session(&record, &s_flow.session, 0, (uint16_t)seconds);
        if (archery_store_save(&s_history, &record) != ESP_OK) {
            s_flow.save_failed = true;
            ESP_LOGE(TAG, "训练记录写入失败");
        }
        archery_flow_set_history_count(&s_flow, s_history.meta.count);
        break;
    }
    case ARCHERY_ACT_CLEAR_HISTORY:
        if (archery_store_clear(&s_history) != ESP_OK) {
            ESP_LOGE(TAG, "清空历史失败，保留原有记录");
        }
        archery_flow_set_history_count(&s_flow, s_history.meta.count);
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
    archery_key_t key;
    if (event == BSP_BTN_LONG) {
        if (!archery_flow_button_is_ok(physical, s_rotate_keys)) return;
        key = ARCHERY_KEY_OK_LONG;
    } else if (event == BSP_BTN_CLICK || event == BSP_BTN_DOUBLE) {
        // 双击窗口内的快速连按会被驱动合并成 DOUBLE；本应用没有双击语义，
        // 按一次单击处理，避免快速调整数值时"按键失灵"。
        key = archery_flow_button_key(physical, s_rotate_keys);
    } else {
        return;
    }
    if (!s_ready || !s_queue) return;
    const key_message_t message = {.key = key};
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

    key_message_t pending;
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

static void worker_task(void *argument) {
    (void)argument;
    s_next_battery_us = esp_timer_get_time();
    for (;;) {
        key_message_t message;
        const TickType_t wait = pdMS_TO_TICKS(500);
        if (xQueueReceive(s_queue, &message, wait) == pdTRUE) {
            s_last_input_us = esp_timer_get_time();
            handle_key(message.key);
            continue;
        }
        const int64_t now = esp_timer_get_time();
        if (now >= s_next_battery_us) {
            refresh_battery();
            s_next_battery_us = now + (int64_t)BATTERY_POLL_MS * 1000;
        }
        const uint16_t off_seconds = s_flow.settings.off_seconds;
        if (off_seconds > 0 &&
            now - s_last_input_us >= (int64_t)off_seconds * 1000000) {
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
    archery_settings_t stored;
    if (archery_settings_load(&stored) != ESP_OK) {
        ESP_LOGW(TAG, "设置读取失败，使用默认亮度/息屏时间");
    }
    archery_flow_set_settings(&s_flow, &stored);
    apply_settings(&s_flow.settings);

    if (archery_store_load(&s_history) != ESP_OK) {
        ESP_LOGW(TAG, "历史记录载入失败，从空历史开始");
        memset(&s_history, 0, sizeof(s_history));
        archery_history_meta_init(&s_history.meta);
    }
    archery_flow_set_history_count(&s_flow, s_history.meta.count);

    s_battery_soc = bsp_battery_soc();
    s_session_start_us = esp_timer_get_time();
    s_last_input_us = s_session_start_us;

    redraw();

    s_queue = xQueueCreate(KEY_QUEUE_DEPTH, sizeof(key_message_t));
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
