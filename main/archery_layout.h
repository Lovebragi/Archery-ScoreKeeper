// main/archery_layout.h —— 与 LVGL 无关的布局计算，便于主机单测。
#pragma once

#include <stdint.h>

#define ARCHERY_LAYOUT_MAX_CELLS 12

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} archery_rect_t;

typedef struct {
    uint8_t cols;
    uint8_t rows;
    int16_t cell_w;
    int16_t cell_h;
    int16_t gap_x;
    int16_t gap_y;
    int16_t origin_x;
    int16_t origin_y;
    uint8_t count;
} archery_grid_t;

// 在指定矩形内为 count 个箭格选择列数与格子尺寸，并水平/垂直居中。
void archery_layout_grid(archery_rect_t area, uint8_t count, archery_grid_t *grid);

// 取第 index 个格子（按行优先）。index 越界时返回零尺寸矩形。
archery_rect_t archery_layout_cell(const archery_grid_t *grid, uint8_t index);

// 列表滚动窗口：固定 visible_rows 行，返回第一个可见行，保证 cursor 在窗口内。
uint8_t archery_layout_list_offset(uint8_t cursor, uint8_t total_rows, uint8_t visible_rows);
