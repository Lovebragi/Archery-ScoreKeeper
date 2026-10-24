// main/archery_layout.c —— 网格与列表窗口计算。
#include "archery_layout.h"

#include <string.h>

#define GRID_GAP 6
#define GRID_CELL_MAX_H 54

void archery_layout_grid(archery_rect_t area, uint8_t count, archery_grid_t *grid) {
    memset(grid, 0, sizeof(*grid));
    if (count == 0 || count > ARCHERY_LAYOUT_MAX_CELLS || area.w <= 0 || area.h <= 0) return;

    uint8_t cols;
    if (count <= 3) {
        cols = count;  // 1~3 箭一行排开
    } else if (count <= 8) {
        cols = 3;      // 4~8 箭排 3 列
    } else {
        cols = 4;      // 9~12 箭排 4 列
    }
    const uint8_t rows = (uint8_t)((count + cols - 1u) / cols);

    int16_t gap_x = GRID_GAP;
    int16_t gap_y = GRID_GAP;
    if (cols > 1 && area.w < (int16_t)(cols * 40)) gap_x = 4;
    if (rows > 1 && area.h < (int16_t)(rows * 40)) gap_y = 4;

    int16_t cell_w = (int16_t)((area.w - (int16_t)(cols - 1) * gap_x) / cols);
    int16_t cell_h = (int16_t)((area.h - (int16_t)(rows - 1) * gap_y) / rows);
    if (cell_h > GRID_CELL_MAX_H) cell_h = GRID_CELL_MAX_H;
    if (cell_w < 8) cell_w = 8;
    if (cell_h < 8) cell_h = 8;

    const int16_t grid_w = (int16_t)(cols * cell_w + (int16_t)(cols - 1) * gap_x);
    const int16_t grid_h = (int16_t)(rows * cell_h + (int16_t)(rows - 1) * gap_y);

    grid->cols = cols;
    grid->rows = rows;
    grid->cell_w = cell_w;
    grid->cell_h = cell_h;
    grid->gap_x = gap_x;
    grid->gap_y = gap_y;
    grid->origin_x = (int16_t)(area.x + (area.w - grid_w) / 2);
    grid->origin_y = (int16_t)(area.y + (area.h - grid_h) / 2);
    grid->count = count;
}

archery_rect_t archery_layout_cell(const archery_grid_t *grid, uint8_t index) {
    archery_rect_t rect = {0, 0, 0, 0};
    if (grid->cols == 0 || index >= grid->count) return rect;
    const uint8_t col = (uint8_t)(index % grid->cols);
    const uint8_t row = (uint8_t)(index / grid->cols);
    rect.w = grid->cell_w;
    rect.h = grid->cell_h;
    rect.x = (int16_t)(grid->origin_x + col * (grid->cell_w + grid->gap_x));
    rect.y = (int16_t)(grid->origin_y + row * (grid->cell_h + grid->gap_y));
    return rect;
}

uint8_t archery_layout_list_offset(uint8_t cursor, uint8_t total_rows, uint8_t visible_rows) {
    if (visible_rows == 0) return 0;
    if (cursor < visible_rows) return 0;
    const uint16_t offset = (uint16_t)cursor - (uint16_t)visible_rows + 1u;
    const uint16_t max_offset =
        total_rows > visible_rows ? (uint16_t)(total_rows - visible_rows) : 0u;
    return (uint8_t)(offset > max_offset ? max_offset : offset);
}
