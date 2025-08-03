
/**
 * @lv_bindings_special.h
 * @brief 特殊 LVGL 绑定头文件
 * @author Sab1e
 * @date 2025-07-29
 */
#ifndef LV_BINDINGS_SPECIAL_H
#define LV_BINDINGS_SPECIAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "jerryscript.h"

// 类型声明

// 函数声明
lv_color_t js_to_lv_color(jerry_value_t js_color);
jerry_value_t lv_color_to_js(lv_color_t color);
void lv_bindings_special_init();

#ifdef __cplusplus
}
#endif

#endif // LV_BINDINGS_SPECIAL_H
