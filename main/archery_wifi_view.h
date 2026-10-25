// main/archery_wifi_view.h —— 联网页面的只读视图（纯数据，无 ESP-IDF 依赖）。
//
// Wi-Fi 任务在后台扫描/连接，结果通过这个结构交给界面与状态机；界面只读，
// 不直接调用 Wi-Fi API，便于主机测试与分层。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define ARCHERY_WIFI_AP_MAX 8u
#define ARCHERY_WIFI_SSID_MAX 33u

typedef enum {
    ARCHERY_WIFI_IDLE = 0,     // 未连接
    ARCHERY_WIFI_SCANNING,     // 正在扫描
    ARCHERY_WIFI_LIST,         // 扫描结果列表
    ARCHERY_WIFI_CONNECTING,   // 正在连接
    ARCHERY_WIFI_CONNECTED,    // 已连接
    ARCHERY_WIFI_FAILED,       // 连接/对时失败
    ARCHERY_WIFI_AP_READY,     // 手机扫码配网中（设备热点已开）
} archery_wifi_state_t;

typedef struct {
    uint8_t state;      // archery_wifi_state_t
    uint8_t ap_count;   // 扫描到的网络数 0..ARCHERY_WIFI_AP_MAX
    uint8_t selected;   // 列表光标
    bool sta_connected; // 是否已连上路由器
    bool time_synced;   // 本次联网后是否已对时成功
    char ssid[ARCHERY_WIFI_AP_MAX][ARCHERY_WIFI_SSID_MAX];
    int8_t rssi[ARCHERY_WIFI_AP_MAX];
    bool secure[ARCHERY_WIFI_AP_MAX];
    char ip[16];
    char message[64];
} archery_wifi_view_t;

// 当前视图（由应用在刷新前写入，界面只读）。
static inline void archery_wifi_view_clear(archery_wifi_view_t *view) {
    if (view) memset(view, 0, sizeof(*view));
}
