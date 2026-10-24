// tests/test_archery_layout.c —— 箭格网格与列表窗口计算测试。
#include "archery_layout.h"

#include <assert.h>
#include <stdio.h>

static int rects_overlap(archery_rect_t a, archery_rect_t b) {
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

static void test_grid_for_every_count(void) {
    const archery_rect_t area = {14, 46, 212, 136};
    for (uint8_t count = 1; count <= ARCHERY_LAYOUT_MAX_CELLS; count++) {
        archery_grid_t grid;
        archery_layout_grid(area, count, &grid);
        assert(grid.count == count);
        assert(grid.cols >= 1 && grid.rows >= 1);
        assert((uint8_t)(grid.cols * grid.rows) >= count);

        archery_rect_t cells[ARCHERY_LAYOUT_MAX_CELLS];
        for (uint8_t i = 0; i < count; i++) {
            cells[i] = archery_layout_cell(&grid, i);
            assert(cells[i].w > 0 && cells[i].h > 0);
            // 完全落在区域内
            assert(cells[i].x >= area.x);
            assert(cells[i].y >= area.y);
            assert(cells[i].x + cells[i].w <= area.x + area.w);
            assert(cells[i].y + cells[i].h <= area.y + area.h);
        }
        // 任意两格不重叠
        for (uint8_t i = 0; i < count; i++) {
            for (uint8_t j = (uint8_t)(i + 1); j < count; j++) {
                assert(!rects_overlap(cells[i], cells[j]));
            }
        }
        // 越界索引返回空矩形
        const archery_rect_t outside = archery_layout_cell(&grid, count);
        assert(outside.w == 0 && outside.h == 0);
    }
}

static void test_grid_column_choice(void) {
    archery_grid_t grid;
    archery_layout_grid((archery_rect_t){14, 46, 212, 136}, 2, &grid);
    assert(grid.cols == 2 && grid.rows == 1);
    archery_layout_grid((archery_rect_t){14, 46, 212, 136}, 6, &grid);
    assert(grid.cols == 3 && grid.rows == 2);
    archery_layout_grid((archery_rect_t){14, 46, 212, 136}, 12, &grid);
    assert(grid.cols == 4 && grid.rows == 3);
    // 0 个格子不产生有效网格
    archery_layout_grid((archery_rect_t){14, 46, 212, 136}, 0, &grid);
    assert(grid.cols == 0 || grid.count == 0);
    // 超出上限同样拒绝
    archery_layout_grid((archery_rect_t){14, 46, 212, 136},
                        ARCHERY_LAYOUT_MAX_CELLS + 1, &grid);
    assert(grid.count == 0);
}

static void test_list_window(void) {
    // 4 行窗口，11 行内容
    assert(archery_layout_list_offset(0, 11, 4) == 0);
    assert(archery_layout_list_offset(3, 11, 4) == 0);
    assert(archery_layout_list_offset(4, 11, 4) == 1);
    assert(archery_layout_list_offset(10, 11, 4) == 7);
    // 内容不足一屏
    assert(archery_layout_list_offset(2, 2, 4) == 0);
    assert(archery_layout_list_offset(0, 0, 4) == 0);
    // 0 行窗口不应越界
    assert(archery_layout_list_offset(5, 11, 0) == 0);
}

int main(void) {
    test_grid_for_every_count();
    test_grid_column_choice();
    test_list_window();
    puts("archery layout tests: PASS");
    return 0;
}
