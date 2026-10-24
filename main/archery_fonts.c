// main/archery_fonts.c —— 运行期字形覆盖自检。
#include "archery_fonts.h"

#include "esp_log.h"

static const char *TAG = "archery_font";

// 最小 UTF-8 解码：只需要码点，不做合法性归一化。非法字节按 U+FFFD 跳过。
static uint32_t next_codepoint(const char **cursor) {
    const unsigned char *p = (const unsigned char *)*cursor;
    uint32_t cp = 0xFFFDu;
    int extra = 0;
    if (p[0] < 0x80u) {
        cp = p[0];
        extra = 0;
    } else if ((p[0] & 0xE0u) == 0xC0u) {
        cp = p[0] & 0x1Fu;
        extra = 1;
    } else if ((p[0] & 0xF0u) == 0xE0u) {
        cp = p[0] & 0x0Fu;
        extra = 2;
    } else if ((p[0] & 0xF8u) == 0xF0u) {
        cp = p[0] & 0x07u;
        extra = 3;
    }
    const unsigned char *q = p + 1;
    for (int i = 0; i < extra; i++, q++) {
        if ((q[0] & 0xC0u) != 0x80u) {
            *cursor = (const char *)q;
            return 0xFFFDu;
        }
        cp = (cp << 6) | (q[0] & 0x3Fu);
    }
    *cursor = (const char *)q;
    return cp;
}

void archery_font_audit(const lv_font_t *font, const char *text) {
    if (!font || !text) return;
    const char *p = text;
    while (*p) {
        const uint32_t cp = next_codepoint(&p);
        if (cp < 0x20u) continue;  // 控制字符不参与覆盖检查
        lv_font_glyph_dsc_t dsc;
        if (!lv_font_get_glyph_dsc(font, &dsc, cp, 0) || dsc.is_placeholder) {
            ESP_LOGW(TAG, "字库缺字形 U+%04lX", (unsigned long)cp);
        }
    }
}
