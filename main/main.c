// main/main.c —— 射箭计分器固件入口。
//
// 应用目标：40 半靶、三联靶与 40/80/122 全靶训练计分，训练前设置组数与每组
// 箭数（默认 6×6，三联靶还要分上/中/下），组内按环值统计箭数、整组确认，成绩
// 带时间戳保存在 NVS（最近 300 条）并可查询历史。硬件能力（显示、按键、电量计）
// 全部通过 components/bsp 复用，界面在 main/archery_ui.c 中重新设计，不使用
// 基线 demo 的测试菜单。
//
// 屏幕方向由设置决定（默认倒装 180°），运行时通过 MADCTL 切换。
#include "archery_app.h"
#include "archery_version.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "main";

static esp_err_t storage_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 分区布局变化或旧数据版本不兼容时重建 NVS；历史记录会随之清空。
        ESP_LOGW(TAG, "NVS 需要重新初始化(%s)，正在格式化", esp_err_to_name(err));
        esp_err_t erase = nvs_flash_erase();
        if (erase != ESP_OK) return erase;
        err = nvs_flash_init();
    }
    return err;
}

void app_main(void) {
    ESP_LOGI(TAG, "%s %s 启动（半靶/三联/全靶，300 条历史）",
             ARCHERY_FIRMWARE_CHANNEL, ARCHERY_FIRMWARE_VERSION);

    bsp_i2c_init();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        // 没有屏幕就没有应用界面：打清楚引脚日志后停止，不做串口菜单降级。
        ESP_LOGE(TAG, "显示/LVGL 初始化失败，检查 SPI 接线"
                      "(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    if (storage_init() != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败，历史记录不可用");
    }
    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "电量计不可用，界面只显示空心电池图标");
    }

    archery_app_start();
    ESP_LOGI(TAG, "启动完成，内部 RAM 空闲 %u 字节",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}
