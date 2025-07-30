
/**
 * @file lv_bindings.c
 * @brief 将 LVGL 绑定到 JerryScript 的实现文件，此文件使用脚本自动生成。
 * @author Sab1e
 * @date 2025-07-31
 */
// Application System header files
#include "lv_bindings.h"
#include "lv_bindings_special.h"
#include "appsys_core.h"
// Third party header files
#include "jerryscript.h"
#include "uthash.h"
#include "lvgl/lvgl.h"

/********************************** 错误处理辅助函数 **********************************/
static jerry_value_t throw_error(const char* message) {
    jerry_value_t error_obj = jerry_error_sz(JERRY_ERROR_TYPE, (const jerry_char_t*)message);
    return jerry_throw_value(error_obj, true);
}

/********************************** 回调系统 **********************************/
#define MAX_CALLBACKS_PER_KEY 8

// 组合键结构体
typedef struct {
    lv_obj_t* obj;
    int event;
} callback_key_t;

// 回调映射表结构体，支持多个 JS 回调
typedef struct {
    callback_key_t key;
    jerry_value_t callbacks[MAX_CALLBACKS_PER_KEY];
    int callback_count;
    UT_hash_handle hh;
} callback_map_t;

static callback_map_t* callback_table = NULL;

/**
 * @brief 处理 LVGL 的事件回调
 * @param e 由 LVGL 传入的事件对象
 */
static void lv_event_handler(lv_event_t* e) {
    lv_obj_t* target = lv_event_get_target(e);
    int event = lv_event_get_code(e);

    callback_map_t* entry = NULL;
    callback_key_t key = { .obj = target, .event = event };
    HASH_FIND(hh, callback_table, &key, sizeof(callback_key_t), entry);

    // 支持 LV_EVENT_ALL 回调查找
    if (!entry) {
        key.event = LV_EVENT_ALL;
        HASH_FIND(hh, callback_table, &key, sizeof(callback_key_t), entry);
    }
    if (!entry) return;

    jerry_value_t event_obj = jerry_object();
    jerry_object_set(event_obj, jerry_string_sz("type"), jerry_number(event));
    jerry_object_set(event_obj, jerry_string_sz("target"), jerry_number((uintptr_t)target));

    jerry_value_t global = jerry_current_realm();
    jerry_value_t args[1] = { event_obj };

    for (int i = 0; i < entry->callback_count; i++) {
        jerry_value_t ret = jerry_call(entry->callbacks[i], global, args, 1);
        jerry_value_free(ret);
    }

    jerry_value_free(event_obj);
}

/**
 * @brief 注册 LVGL 事件处理函数
 * @param args[0] lv_obj_t 对象
 * @param args[1] LVGL 事件类型（整数）
 * @param args[2] JavaScript 函数作为事件处理器
 * @return 无返回或抛出异常
 */
static jerry_value_t register_lv_event_handler(const jerry_call_info_t* call_info_p,
    const jerry_value_t args[],
    const jerry_length_t arg_cnt) {
    if (arg_cnt < 3 || !jerry_value_is_object(args[0]) ||
        !jerry_value_is_number(args[1]) || !jerry_value_is_function(args[2])) {
        return throw_error("Invalid arguments");
    }

    jerry_value_t ptr_val = jerry_object_get(args[0], jerry_string_sz("__ptr"));
    if (!jerry_value_is_number(ptr_val)) {
        jerry_value_free(ptr_val);
        return throw_error("Invalid __ptr");
    }
    lv_obj_t* obj = (lv_obj_t*)(uintptr_t)jerry_value_as_number(ptr_val);
    jerry_value_free(ptr_val);

    int event = (int)jerry_value_as_number(args[1]);
    jerry_value_t js_func = jerry_value_copy(args[2]);

    callback_map_t* entry = NULL;
    callback_key_t key = { .obj = obj, .event = event };
    HASH_FIND(hh, callback_table, &key, sizeof(callback_key_t), entry);

    if (!entry) {
        entry = malloc(sizeof(callback_map_t));
        entry->key = key;
        entry->callback_count = 0;
        memset(entry->callbacks, 0, sizeof(entry->callbacks));
        HASH_ADD(hh, callback_table, key, sizeof(callback_key_t), entry);
        lv_obj_add_event_cb(obj, lv_event_handler, event, NULL);
    }

    if (entry->callback_count < MAX_CALLBACKS_PER_KEY) {
        entry->callbacks[entry->callback_count++] = js_func;
    }
    else {
        jerry_value_free(js_func);
        return throw_error("Too many callbacks");
    }

    return jerry_undefined();
}

/**
 * @brief 取消注册 LVGL 事件处理函数
 * @param args[0] lv_obj_t 对象
 * @param args[1] LVGL 事件类型（整数）
 * @return 无返回或抛出异常
 */
static jerry_value_t unregister_lv_event_handler(const jerry_call_info_t* call_info_p,
    const jerry_value_t args[],
    const jerry_length_t arg_cnt) {
    if (arg_cnt < 2 || !jerry_value_is_object(args[0]) || !jerry_value_is_number(args[1])) {
        return throw_error("Invalid arguments");
    }

    jerry_value_t ptr_val = jerry_object_get(args[0], jerry_string_sz("__ptr"));
    if (!jerry_value_is_number(ptr_val)) {
        jerry_value_free(ptr_val);
        return throw_error("Invalid __ptr");
    }
    lv_obj_t* obj = (lv_obj_t*)(uintptr_t)jerry_value_as_number(ptr_val);
    jerry_value_free(ptr_val);

    int event = (int)jerry_value_as_number(args[1]);

    callback_map_t* entry = NULL;
    callback_key_t key = { .obj = obj, .event = event };
    HASH_FIND(hh, callback_table, &key, sizeof(callback_key_t), entry);

    if (entry) {
        for (int i = 0; i < entry->callback_count; i++) {
            jerry_value_free(entry->callbacks[i]);
        }
        HASH_DEL(callback_table, entry);
        free(entry);
    }

    return jerry_undefined();
}

/**
 * @brief 当 LVGL 对象被删除时，清理回调映射表中的对应条目
 * @param e 由 LVGL 传入的事件对象
 */
static void lv_obj_deleted_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target(e);
    callback_map_t* cur, * tmp;
    HASH_ITER(hh, callback_table, cur, tmp) {
        if (cur->key.obj == obj) {
        for (int i = 0; i < cur->callback_count; i++) {
            jerry_value_free(cur->callbacks[i]);
        }
        HASH_DEL(callback_table, cur);
        free(cur);
        }
    }
}

/********************************** 函数系统 **********************************/

// 解析 lv_color_t 参数
static lv_color_t parse_lv_color(jerry_value_t js_color) {
    lv_color_t color;

    if (jerry_value_is_number(js_color)) {
        uint32_t val = (uint32_t)jerry_value_as_number(js_color);
        color.red = (val >> 16) & 0xFF;
        color.green = (val >> 8) & 0xFF;
        color.blue = val & 0xFF;
        return color;
    }

    if (!jerry_value_is_object(js_color)) {
        color.red = 0;
        color.green = 0;
        color.blue = 0;
        return color;
    }

    jerry_value_t r_val = jerry_object_get(js_color, jerry_string_sz("red"));
    jerry_value_t g_val = jerry_object_get(js_color, jerry_string_sz("green"));
    jerry_value_t b_val = jerry_object_get(js_color, jerry_string_sz("blue"));

    color.red = jerry_value_is_number(r_val) ? (uint8_t)jerry_value_as_number(r_val) : 0;
    color.green = jerry_value_is_number(g_val) ? (uint8_t)jerry_value_as_number(g_val) : 0;
    color.blue = jerry_value_is_number(b_val) ? (uint8_t)jerry_value_as_number(b_val) : 0;

    jerry_value_free(r_val);
    jerry_value_free(g_val);
    jerry_value_free(b_val);

    return color;
}
// 函数声明
static jerry_value_t js_lv_label_set_text(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_obj_add_flag(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_obj_set_pos(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_bar_set_range(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_checkbox_create(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_slider_create(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_obj_create(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_obj_set_state(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_chart_set_range(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_obj_set_style_radius(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_obj_clean(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_slider_set_range(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_textarea_set_text(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_event_get_target(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_msgbox_close(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_obj_set_style_pad_row(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_event_get_user_data(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_arc_set_range(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_obj_set_style_pad_all(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_obj_set_style_border_color(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_obj_set_style_bg_color(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_arc_set_value(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_label_create(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_dropdown_create(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_obj_align_to(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_table_set_row_cnt(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_obj_del(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_checkbox_set_text(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_img_set_zoom(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_event_get_code(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_arc_set_bg_angles(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_table_set_cell_value(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_obj_set_size(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_arc_create(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_obj_set_style_text_color(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_label_set_long_mode(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_table_create(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_textarea_create(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_slider_set_value(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_bar_set_value(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_btn_create(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_label_set_recolor(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_obj_align(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_chart_create(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_roller_set_selected(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_textarea_set_placeholder_text(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_obj_add_style(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_timer_handler(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_textarea_add_text(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_dropdown_get_selected(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_msgbox_create(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_obj_clear_flag(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_roller_create(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_obj_set_style_text_font(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_bar_create(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_dropdown_set_options(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_slider_get_value(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_table_set_col_cnt(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_obj_set_style_border_width(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_roller_set_options(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_switch_create(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_obj_set_width(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_scr_act(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_chart_set_type(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_obj_center(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_delay_ms(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_img_create(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_obj_set_style_pad_column(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_dropdown_set_selected(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_chart_set_point_count(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_img_set_angle(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);
static jerry_value_t js_lv_label_get_text(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);

// 函数实现

/**
 * Set a new text for a label. Memory will be allocated to store the text by the label.
 */
static jerry_value_t js_lv_label_set_text(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 2) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: text (const char*)
    jerry_value_t js_text = args[1];
    if (!jerry_value_is_string(js_text)) {
        return throw_error("Argument 1 must be a string");
    }
    
    jerry_size_t text_len = jerry_string_size(js_text, JERRY_ENCODING_UTF8);
    char* text = (char*)malloc(text_len + 1);
    if (!text) {
        return throw_error("Out of memory");
    }
    jerry_string_to_buffer(js_text, JERRY_ENCODING_UTF8, (jerry_char_t*)text, text_len);
    text[text_len] = '\0';

    // 调用底层函数
    lv_label_set_text(obj, text);
    free(text);
    return jerry_undefined();
}



/**
 * Set one or more flags
 */
static jerry_value_t js_lv_obj_add_flag(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 2) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: f (lv_obj_flag_t)
    jerry_value_t js_f = args[1];
    if (!jerry_value_is_number(js_f)) {
        return throw_error("Argument 1 must be a number");
    }
    
    int f = (int)jerry_value_as_number(js_f);
    
    // 调用底层函数
    lv_obj_add_flag(obj, f);
    return jerry_undefined();
}



/**
 * Set the position of an object relative to the set alignment. With default alignment it's the distance from the top left corner  E.g. LV_ALIGN_CENTER alignment it's the offset from the center of the parent  The position is interpreted on the content area of the parent  The values can be set in pixel or in percentage of parent size with lv_pct(v)
 */
static jerry_value_t js_lv_obj_set_pos(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 3) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: x (int32_t)
    jerry_value_t js_x = args[1];
    if (!jerry_value_is_number(js_x)) {
        return throw_error("Argument 1 must be a number");
    }
    
    int32_t x = (int32_t)jerry_value_as_number(js_x);
    
    // 解析参数: y (int32_t)
    jerry_value_t js_y = args[2];
    if (!jerry_value_is_number(js_y)) {
        return throw_error("Argument 2 must be a number");
    }
    
    int32_t y = (int32_t)jerry_value_as_number(js_y);
    
    // 调用底层函数
    lv_obj_set_pos(obj, x, y);
    return jerry_undefined();
}



/**
 * Set minimum and the maximum values of a bar If min is greater than max, the drawing direction becomes to the opposite direction.
 */
static jerry_value_t js_lv_bar_set_range(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 3) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: min (int32_t)
    jerry_value_t js_min = args[1];
    if (!jerry_value_is_number(js_min)) {
        return throw_error("Argument 1 must be a number");
    }
    
    int32_t min = (int32_t)jerry_value_as_number(js_min);
    
    // 解析参数: max (int32_t)
    jerry_value_t js_max = args[2];
    if (!jerry_value_is_number(js_max)) {
        return throw_error("Argument 2 must be a number");
    }
    
    int32_t max = (int32_t)jerry_value_as_number(js_max);
    
    // 调用底层函数
    lv_bar_set_range(obj, min, max);
    return jerry_undefined();
}



/**
 * Create a check box object pointer to the created check box
 */
static jerry_value_t js_lv_checkbox_create(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: parent (lv_obj_t*)
    jerry_value_t js_parent = args[0];
    if (!jerry_value_is_object(js_parent)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t parent_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t parent_ptr_val = jerry_object_get(js_parent, parent_ptr_prop);
    jerry_value_free(parent_ptr_prop);
    
    if (!jerry_value_is_number(parent_ptr_val)) {
        jerry_value_free(parent_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t parent_ptr = (uintptr_t)jerry_value_as_number(parent_ptr_val);
    jerry_value_free(parent_ptr_val);
    void* parent = (void*)parent_ptr;
    
    // 调用底层函数
    lv_obj_t* ret_value = lv_checkbox_create(parent);

    // 处理返回值
    // 包装为LVGL对象
    jerry_value_t js_obj = jerry_object();
    jerry_value_t ptr = jerry_number((double)(uintptr_t)ret_value);
    jerry_value_t cls = jerry_string_sz("lv_obj");
    
    jerry_object_set(js_obj, jerry_string_sz("__ptr"), ptr);
    jerry_object_set(js_obj, jerry_string_sz("__class"), cls);
    
    jerry_value_free(ptr);
    jerry_value_free(cls);
    return js_obj;
}



/**
 * Create a slider object pointer to the created slider
 */
static jerry_value_t js_lv_slider_create(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: parent (lv_obj_t*)
    jerry_value_t js_parent = args[0];
    if (!jerry_value_is_object(js_parent)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t parent_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t parent_ptr_val = jerry_object_get(js_parent, parent_ptr_prop);
    jerry_value_free(parent_ptr_prop);
    
    if (!jerry_value_is_number(parent_ptr_val)) {
        jerry_value_free(parent_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t parent_ptr = (uintptr_t)jerry_value_as_number(parent_ptr_val);
    jerry_value_free(parent_ptr_val);
    void* parent = (void*)parent_ptr;
    
    // 调用底层函数
    lv_obj_t* ret_value = lv_slider_create(parent);

    // 处理返回值
    // 包装为LVGL对象
    jerry_value_t js_obj = jerry_object();
    jerry_value_t ptr = jerry_number((double)(uintptr_t)ret_value);
    jerry_value_t cls = jerry_string_sz("lv_obj");
    
    jerry_object_set(js_obj, jerry_string_sz("__ptr"), ptr);
    jerry_object_set(js_obj, jerry_string_sz("__class"), cls);
    
    jerry_value_free(ptr);
    jerry_value_free(cls);
    return js_obj;
}



/**
 * Create a base object (a rectangle) pointer to the new object
 */
static jerry_value_t js_lv_obj_create(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: parent (lv_obj_t*)
    jerry_value_t js_parent = args[0];
    if (!jerry_value_is_object(js_parent)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t parent_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t parent_ptr_val = jerry_object_get(js_parent, parent_ptr_prop);
    jerry_value_free(parent_ptr_prop);
    
    if (!jerry_value_is_number(parent_ptr_val)) {
        jerry_value_free(parent_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t parent_ptr = (uintptr_t)jerry_value_as_number(parent_ptr_val);
    jerry_value_free(parent_ptr_val);
    void* parent = (void*)parent_ptr;
    
    // 调用底层函数
    lv_obj_t* ret_value = lv_obj_create(parent);

    // 处理返回值
    // 包装为LVGL对象
    jerry_value_t js_obj = jerry_object();
    jerry_value_t ptr = jerry_number((double)(uintptr_t)ret_value);
    jerry_value_t cls = jerry_string_sz("lv_obj");
    
    jerry_object_set(js_obj, jerry_string_sz("__ptr"), ptr);
    jerry_object_set(js_obj, jerry_string_sz("__class"), cls);
    
    jerry_value_free(ptr);
    jerry_value_free(cls);
    return js_obj;
}



/**
 * Add or remove one or more states to the object. The other state bits will remain unchanged.
 */
static jerry_value_t js_lv_obj_set_state(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 3) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: state (lv_state_t)
    jerry_value_t js_state = args[1];
    if (!jerry_value_is_number(js_state)) {
        return throw_error("Argument 1 must be a number");
    }
    
    uint16_t state = (uint16_t)jerry_value_as_number(js_state);
    
    // 解析参数: v (bool)
    // 布尔类型或兼容类型参数: v
    bool v = false;
    if (!jerry_value_is_undefined(args[2])) {
        if (jerry_value_is_boolean(args[2])) {
            v = jerry_value_to_boolean(args[2]);
        }
        else if (jerry_value_is_number(args[2])) {
            v = (jerry_value_as_number(args[2]) != 0);
        }
        else {
            return throw_error("Argument 2 must be boolean or number for bool");
        }
    }
    
    // 调用底层函数
    lv_obj_set_state(obj, state, v);
    return jerry_undefined();
}



/**
 * Set the minimal and maximal y values on an axis
 */
static jerry_value_t js_lv_chart_set_range(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 4) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: axis (lv_chart_axis_t)
    jerry_value_t js_axis = args[1];
    if (!jerry_value_is_number(js_axis)) {
        return throw_error("Argument 1 must be a number");
    }
    
    int axis = (int)jerry_value_as_number(js_axis);
    
    // 解析参数: min (int32_t)
    jerry_value_t js_min = args[2];
    if (!jerry_value_is_number(js_min)) {
        return throw_error("Argument 2 must be a number");
    }
    
    int32_t min = (int32_t)jerry_value_as_number(js_min);
    
    // 解析参数: max (int32_t)
    jerry_value_t js_max = args[3];
    if (!jerry_value_is_number(js_max)) {
        return throw_error("Argument 3 must be a number");
    }
    
    int32_t max = (int32_t)jerry_value_as_number(js_max);
    
    // 调用底层函数
    lv_chart_set_axis_range(obj, axis, min, max);
    return jerry_undefined();
}



/**
 * lv_obj_set_style_radius function (aliased to lv_obj_set_style_radius)
 */
static jerry_value_t js_lv_obj_set_style_radius(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 3) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: value (int32_t)
    jerry_value_t js_value = args[1];
    if (!jerry_value_is_number(js_value)) {
        return throw_error("Argument 1 must be a number");
    }
    
    int32_t value = (int32_t)jerry_value_as_number(js_value);
    
    // 解析参数: selector (lv_style_selector_t)
    jerry_value_t js_selector = args[2];
    if (!jerry_value_is_number(js_selector)) {
        return throw_error("Argument 2 must be a number");
    }
    
    uint32_t selector = (uint32_t)jerry_value_as_number(js_selector);
    
    // 调用底层函数
    lv_obj_set_style_radius(obj, value, selector);
    return jerry_undefined();
}



/**
 * Delete all children of an object. Also remove the objects from their group and remove all animations (if any). Send LV_EVENT_DELETED to deleted objects.
 */
static jerry_value_t js_lv_obj_clean(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 调用底层函数
    lv_obj_clean(obj);
    return jerry_undefined();
}



/**
 * Set minimum and the maximum values of a bar
 */
static jerry_value_t js_lv_slider_set_range(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 3) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: min (int32_t)
    jerry_value_t js_min = args[1];
    if (!jerry_value_is_number(js_min)) {
        return throw_error("Argument 1 must be a number");
    }
    
    int32_t min = (int32_t)jerry_value_as_number(js_min);
    
    // 解析参数: max (int32_t)
    jerry_value_t js_max = args[2];
    if (!jerry_value_is_number(js_max)) {
        return throw_error("Argument 2 must be a number");
    }
    
    int32_t max = (int32_t)jerry_value_as_number(js_max);
    
    // 调用底层函数
    lv_slider_set_range(obj, min, max);
    return jerry_undefined();
}



/**
 * Set the text of a text area
 */
static jerry_value_t js_lv_textarea_set_text(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 2) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: txt (const char*)
    jerry_value_t js_txt = args[1];
    if (!jerry_value_is_string(js_txt)) {
        return throw_error("Argument 1 must be a string");
    }
    
    jerry_size_t txt_len = jerry_string_size(js_txt, JERRY_ENCODING_UTF8);
    char* txt = (char*)malloc(txt_len + 1);
    if (!txt) {
        return throw_error("Out of memory");
    }
    jerry_string_to_buffer(js_txt, JERRY_ENCODING_UTF8, (jerry_char_t*)txt, txt_len);
    txt[txt_len] = '\0';

    // 调用底层函数
    lv_textarea_set_text(obj, txt);
    free(txt);
    return jerry_undefined();
}



/**
 * Get Widget originally targeted by the event. It's the same even if event was bubbled. the target of the event_code
 */
static jerry_value_t js_lv_event_get_target(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: e (lv_event_t*)
    jerry_value_t js_e = args[0];
    if (!jerry_value_is_object(js_e)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t e_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t e_ptr_val = jerry_object_get(js_e, e_ptr_prop);
    jerry_value_free(e_ptr_prop);
    
    if (!jerry_value_is_number(e_ptr_val)) {
        jerry_value_free(e_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t e_ptr = (uintptr_t)jerry_value_as_number(e_ptr_val);
    jerry_value_free(e_ptr_val);
    void* e = (void*)e_ptr;
    
    // 调用底层函数
    void* ret_value = lv_event_get_target(e);

    // 处理返回值
    // 包装为通用指针对象
    jerry_value_t js_obj = jerry_object();
    jerry_value_t ptr = jerry_number((double)(uintptr_t)ret_value);
    
    jerry_object_set(js_obj, jerry_string_sz("__ptr"), ptr);
    jerry_object_set(js_obj, jerry_string_sz("__type"), jerry_string_sz("void*"));
    
    jerry_value_free(ptr);
    return js_obj;
}



/**
 * Close a message box
 */
static jerry_value_t js_lv_msgbox_close(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: mbox (lv_obj_t*)
    jerry_value_t js_mbox = args[0];
    if (!jerry_value_is_object(js_mbox)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t mbox_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t mbox_ptr_val = jerry_object_get(js_mbox, mbox_ptr_prop);
    jerry_value_free(mbox_ptr_prop);
    
    if (!jerry_value_is_number(mbox_ptr_val)) {
        jerry_value_free(mbox_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t mbox_ptr = (uintptr_t)jerry_value_as_number(mbox_ptr_val);
    jerry_value_free(mbox_ptr_val);
    void* mbox = (void*)mbox_ptr;
    
    // 调用底层函数
    lv_msgbox_close(mbox);
    return jerry_undefined();
}



/**
 * lv_obj_set_style_pad_row function (aliased to lv_obj_set_style_pad_row)
 */
static jerry_value_t js_lv_obj_set_style_pad_row(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 3) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: value (int32_t)
    jerry_value_t js_value = args[1];
    if (!jerry_value_is_number(js_value)) {
        return throw_error("Argument 1 must be a number");
    }
    
    int32_t value = (int32_t)jerry_value_as_number(js_value);
    
    // 解析参数: selector (lv_style_selector_t)
    jerry_value_t js_selector = args[2];
    if (!jerry_value_is_number(js_selector)) {
        return throw_error("Argument 2 must be a number");
    }
    
    uint32_t selector = (uint32_t)jerry_value_as_number(js_selector);
    
    // 调用底层函数
    lv_obj_set_style_pad_row(obj, value, selector);
    return jerry_undefined();
}



/**
 * Get user_data passed when event was registered on Widget. pointer to the user_data
 */
static jerry_value_t js_lv_event_get_user_data(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: e (lv_event_t*)
    jerry_value_t js_e = args[0];
    if (!jerry_value_is_object(js_e)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t e_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t e_ptr_val = jerry_object_get(js_e, e_ptr_prop);
    jerry_value_free(e_ptr_prop);
    
    if (!jerry_value_is_number(e_ptr_val)) {
        jerry_value_free(e_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t e_ptr = (uintptr_t)jerry_value_as_number(e_ptr_val);
    jerry_value_free(e_ptr_val);
    void* e = (void*)e_ptr;
    
    // 调用底层函数
    void* ret_value = lv_event_get_user_data(e);

    // 处理返回值
    // 包装为通用指针对象
    jerry_value_t js_obj = jerry_object();
    jerry_value_t ptr = jerry_number((double)(uintptr_t)ret_value);
    
    jerry_object_set(js_obj, jerry_string_sz("__ptr"), ptr);
    jerry_object_set(js_obj, jerry_string_sz("__type"), jerry_string_sz("void*"));
    
    jerry_value_free(ptr);
    return js_obj;
}



/**
 * Set minimum and the maximum values of an arc
 */
static jerry_value_t js_lv_arc_set_range(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 3) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: min (int32_t)
    jerry_value_t js_min = args[1];
    if (!jerry_value_is_number(js_min)) {
        return throw_error("Argument 1 must be a number");
    }
    
    int32_t min = (int32_t)jerry_value_as_number(js_min);
    
    // 解析参数: max (int32_t)
    jerry_value_t js_max = args[2];
    if (!jerry_value_is_number(js_max)) {
        return throw_error("Argument 2 must be a number");
    }
    
    int32_t max = (int32_t)jerry_value_as_number(js_max);
    
    // 调用底层函数
    lv_arc_set_range(obj, min, max);
    return jerry_undefined();
}



/**
 * lv_obj_set_style_pad_all function (aliased to lv_obj_set_style_pad_all)
 */
static jerry_value_t js_lv_obj_set_style_pad_all(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 3) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: value (int32_t)
    jerry_value_t js_value = args[1];
    if (!jerry_value_is_number(js_value)) {
        return throw_error("Argument 1 must be a number");
    }
    
    int32_t value = (int32_t)jerry_value_as_number(js_value);
    
    // 解析参数: selector (lv_style_selector_t)
    jerry_value_t js_selector = args[2];
    if (!jerry_value_is_number(js_selector)) {
        return throw_error("Argument 2 must be a number");
    }
    
    uint32_t selector = (uint32_t)jerry_value_as_number(js_selector);
    
    // 调用底层函数
    lv_obj_set_style_pad_all(obj, value, selector);
    return jerry_undefined();
}



/**
 * lv_obj_set_style_border_color function (aliased to lv_obj_set_style_border_color)
 */
static jerry_value_t js_lv_obj_set_style_border_color(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 3) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: value (lv_color_t)
    lv_color_t value = parse_lv_color(args[1]);

    // 解析参数: selector (lv_style_selector_t)
    jerry_value_t js_selector = args[2];
    if (!jerry_value_is_number(js_selector)) {
        return throw_error("Argument 2 must be a number");
    }
    
    uint32_t selector = (uint32_t)jerry_value_as_number(js_selector);
    
    // 调用底层函数
    lv_obj_set_style_border_color(obj, value, selector);
    return jerry_undefined();
}



/**
 * lv_obj_set_style_bg_color function (aliased to lv_obj_set_style_bg_color)
 */
static jerry_value_t js_lv_obj_set_style_bg_color(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 3) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: value (lv_color_t)
    lv_color_t value = parse_lv_color(args[1]);

    // 解析参数: selector (lv_style_selector_t)
    jerry_value_t js_selector = args[2];
    if (!jerry_value_is_number(js_selector)) {
        return throw_error("Argument 2 must be a number");
    }
    
    uint32_t selector = (uint32_t)jerry_value_as_number(js_selector);
    
    // 调用底层函数
    lv_obj_set_style_bg_color(obj, value, selector);
    return jerry_undefined();
}



/**
 * Set a new value on the arc
 */
static jerry_value_t js_lv_arc_set_value(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 2) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: value (int32_t)
    jerry_value_t js_value = args[1];
    if (!jerry_value_is_number(js_value)) {
        return throw_error("Argument 1 must be a number");
    }
    
    int32_t value = (int32_t)jerry_value_as_number(js_value);
    
    // 调用底层函数
    lv_arc_set_value(obj, value);
    return jerry_undefined();
}



/**
 * Create a label object pointer to the created button
 */
static jerry_value_t js_lv_label_create(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: parent (lv_obj_t*)
    jerry_value_t js_parent = args[0];
    if (!jerry_value_is_object(js_parent)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t parent_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t parent_ptr_val = jerry_object_get(js_parent, parent_ptr_prop);
    jerry_value_free(parent_ptr_prop);
    
    if (!jerry_value_is_number(parent_ptr_val)) {
        jerry_value_free(parent_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t parent_ptr = (uintptr_t)jerry_value_as_number(parent_ptr_val);
    jerry_value_free(parent_ptr_val);
    void* parent = (void*)parent_ptr;
    
    // 调用底层函数
    lv_obj_t* ret_value = lv_label_create(parent);

    // 处理返回值
    // 包装为LVGL对象
    jerry_value_t js_obj = jerry_object();
    jerry_value_t ptr = jerry_number((double)(uintptr_t)ret_value);
    jerry_value_t cls = jerry_string_sz("lv_obj");
    
    jerry_object_set(js_obj, jerry_string_sz("__ptr"), ptr);
    jerry_object_set(js_obj, jerry_string_sz("__class"), cls);
    
    jerry_value_free(ptr);
    jerry_value_free(cls);
    return js_obj;
}



/**
 * Create a drop-down list object pointer to the created drop-down list
 */
static jerry_value_t js_lv_dropdown_create(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: parent (lv_obj_t*)
    jerry_value_t js_parent = args[0];
    if (!jerry_value_is_object(js_parent)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t parent_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t parent_ptr_val = jerry_object_get(js_parent, parent_ptr_prop);
    jerry_value_free(parent_ptr_prop);
    
    if (!jerry_value_is_number(parent_ptr_val)) {
        jerry_value_free(parent_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t parent_ptr = (uintptr_t)jerry_value_as_number(parent_ptr_val);
    jerry_value_free(parent_ptr_val);
    void* parent = (void*)parent_ptr;
    
    // 调用底层函数
    lv_obj_t* ret_value = lv_dropdown_create(parent);

    // 处理返回值
    // 包装为LVGL对象
    jerry_value_t js_obj = jerry_object();
    jerry_value_t ptr = jerry_number((double)(uintptr_t)ret_value);
    jerry_value_t cls = jerry_string_sz("lv_obj");
    
    jerry_object_set(js_obj, jerry_string_sz("__ptr"), ptr);
    jerry_object_set(js_obj, jerry_string_sz("__class"), cls);
    
    jerry_value_free(ptr);
    jerry_value_free(cls);
    return js_obj;
}



/**
 * Align an object to another object. if the position or size of base changes obj needs to be aligned manually again
 */
static jerry_value_t js_lv_obj_align_to(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 5) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: base (const lv_obj_t*)
    jerry_value_t js_base = args[1];
    if (!jerry_value_is_object(js_base)) {
        return throw_error("Argument 1 must be an object");
    }
    
    jerry_value_t base_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t base_ptr_val = jerry_object_get(js_base, base_ptr_prop);
    jerry_value_free(base_ptr_prop);
    
    if (!jerry_value_is_number(base_ptr_val)) {
        jerry_value_free(base_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t base_ptr = (uintptr_t)jerry_value_as_number(base_ptr_val);
    jerry_value_free(base_ptr_val);
    void* base = (void*)base_ptr;
    
    // 解析参数: align (lv_align_t)
    jerry_value_t js_align = args[2];
    if (!jerry_value_is_number(js_align)) {
        return throw_error("Argument 2 must be a number");
    }
    
    int align = (int)jerry_value_as_number(js_align);
    
    // 解析参数: x_ofs (int32_t)
    jerry_value_t js_x_ofs = args[3];
    if (!jerry_value_is_number(js_x_ofs)) {
        return throw_error("Argument 3 must be a number");
    }
    
    int32_t x_ofs = (int32_t)jerry_value_as_number(js_x_ofs);
    
    // 解析参数: y_ofs (int32_t)
    jerry_value_t js_y_ofs = args[4];
    if (!jerry_value_is_number(js_y_ofs)) {
        return throw_error("Argument 4 must be a number");
    }
    
    int32_t y_ofs = (int32_t)jerry_value_as_number(js_y_ofs);
    
    // 调用底层函数
    lv_obj_align_to(obj, base, align, x_ofs, y_ofs);
    return jerry_undefined();
}



/**
 * Set the number of rows
 */
static jerry_value_t js_lv_table_set_row_cnt(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 2) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: row_cnt (uint32_t)
    jerry_value_t js_row_cnt = args[1];
    if (!jerry_value_is_number(js_row_cnt)) {
        return throw_error("Argument 1 must be a number");
    }
    
    uint32_t row_cnt = (uint32_t)jerry_value_as_number(js_row_cnt);
    
    // 调用底层函数
    lv_table_set_row_count(obj, row_cnt);
    return jerry_undefined();
}



/**
 * Delete an object and all of its children. Also remove the objects from their group and remove all animations (if any). Send LV_EVENT_DELETED to deleted objects.
 */
static jerry_value_t js_lv_obj_del(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 调用底层函数
    lv_obj_delete(obj);
    return jerry_undefined();
}



/**
 * Set the text of a check box. txt will be copied and may be deallocated after this function returns.
 */
static jerry_value_t js_lv_checkbox_set_text(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 2) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: txt (const char*)
    jerry_value_t js_txt = args[1];
    if (!jerry_value_is_string(js_txt)) {
        return throw_error("Argument 1 must be a string");
    }
    
    jerry_size_t txt_len = jerry_string_size(js_txt, JERRY_ENCODING_UTF8);
    char* txt = (char*)malloc(txt_len + 1);
    if (!txt) {
        return throw_error("Out of memory");
    }
    jerry_string_to_buffer(js_txt, JERRY_ENCODING_UTF8, (jerry_char_t*)txt, txt_len);
    txt[txt_len] = '\0';

    // 调用底层函数
    lv_checkbox_set_text(obj, txt);
    free(txt);
    return jerry_undefined();
}



/**
 * Set the zoom factor of the image. Note that indexed and alpha only images can't be transformed.
 */
static jerry_value_t js_lv_img_set_zoom(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 2) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: zoom (uint32_t)
    jerry_value_t js_zoom = args[1];
    if (!jerry_value_is_number(js_zoom)) {
        return throw_error("Argument 1 must be a number");
    }
    
    uint32_t zoom = (uint32_t)jerry_value_as_number(js_zoom);
    
    // 调用底层函数
    lv_image_set_scale(obj, zoom);
    return jerry_undefined();
}



/**
 * Get event code of an event. the event code. (E.g. LV_EVENT_CLICKED , LV_EVENT_FOCUSED , etc)
 */
static jerry_value_t js_lv_event_get_code(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: e (lv_event_t*)
    jerry_value_t js_e = args[0];
    if (!jerry_value_is_object(js_e)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t e_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t e_ptr_val = jerry_object_get(js_e, e_ptr_prop);
    jerry_value_free(e_ptr_prop);
    
    if (!jerry_value_is_number(e_ptr_val)) {
        jerry_value_free(e_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t e_ptr = (uintptr_t)jerry_value_as_number(e_ptr_val);
    jerry_value_free(e_ptr_val);
    void* e = (void*)e_ptr;
    
    // 调用底层函数
    lv_event_code_t ret_value = lv_event_get_code(e);

    // 处理返回值
    return jerry_number(ret_value);
}



/**
 * Set the start and end angles of the arc background
 */
static jerry_value_t js_lv_arc_set_bg_angles(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 3) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: start (lv_value_precise_t)
    jerry_value_t js_start = args[1];
    if (!jerry_value_is_number(js_start)) {
        return throw_error("Argument 1 must be a number");
    }
    
    float start = (float)jerry_value_as_number(js_start);
    
    // 解析参数: end (lv_value_precise_t)
    jerry_value_t js_end = args[2];
    if (!jerry_value_is_number(js_end)) {
        return throw_error("Argument 2 must be a number");
    }
    
    float end = (float)jerry_value_as_number(js_end);
    
    // 调用底层函数
    lv_arc_set_bg_angles(obj, start, end);
    return jerry_undefined();
}



/**
 * Set the value of a cell. New roes/columns are added automatically if required
 */
static jerry_value_t js_lv_table_set_cell_value(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 4) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: row (uint32_t)
    jerry_value_t js_row = args[1];
    if (!jerry_value_is_number(js_row)) {
        return throw_error("Argument 1 must be a number");
    }
    
    uint32_t row = (uint32_t)jerry_value_as_number(js_row);
    
    // 解析参数: col (uint32_t)
    jerry_value_t js_col = args[2];
    if (!jerry_value_is_number(js_col)) {
        return throw_error("Argument 2 must be a number");
    }
    
    uint32_t col = (uint32_t)jerry_value_as_number(js_col);
    
    // 解析参数: txt (const char*)
    jerry_value_t js_txt = args[3];
    if (!jerry_value_is_string(js_txt)) {
        return throw_error("Argument 3 must be a string");
    }
    
    jerry_size_t txt_len = jerry_string_size(js_txt, JERRY_ENCODING_UTF8);
    char* txt = (char*)malloc(txt_len + 1);
    if (!txt) {
        return throw_error("Out of memory");
    }
    jerry_string_to_buffer(js_txt, JERRY_ENCODING_UTF8, (jerry_char_t*)txt, txt_len);
    txt[txt_len] = '\0';

    // 调用底层函数
    lv_table_set_cell_value(obj, row, col, txt);
    free(txt);
    return jerry_undefined();
}



/**
 * Set the size of an object. possible values are: pixel simple set the size accordingly LV_SIZE_CONTENT set the size to involve all children in the given direction lv_pct(x) to set size in percentage of the parent's content area size (the size without paddings). x should be in [0..1000]% range
 */
static jerry_value_t js_lv_obj_set_size(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 3) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: w (int32_t)
    jerry_value_t js_w = args[1];
    if (!jerry_value_is_number(js_w)) {
        return throw_error("Argument 1 must be a number");
    }
    
    int32_t w = (int32_t)jerry_value_as_number(js_w);
    
    // 解析参数: h (int32_t)
    jerry_value_t js_h = args[2];
    if (!jerry_value_is_number(js_h)) {
        return throw_error("Argument 2 must be a number");
    }
    
    int32_t h = (int32_t)jerry_value_as_number(js_h);
    
    // 调用底层函数
    lv_obj_set_size(obj, w, h);
    return jerry_undefined();
}



/**
 * Create an arc object pointer to the created arc
 */
static jerry_value_t js_lv_arc_create(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: parent (lv_obj_t*)
    jerry_value_t js_parent = args[0];
    if (!jerry_value_is_object(js_parent)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t parent_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t parent_ptr_val = jerry_object_get(js_parent, parent_ptr_prop);
    jerry_value_free(parent_ptr_prop);
    
    if (!jerry_value_is_number(parent_ptr_val)) {
        jerry_value_free(parent_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t parent_ptr = (uintptr_t)jerry_value_as_number(parent_ptr_val);
    jerry_value_free(parent_ptr_val);
    void* parent = (void*)parent_ptr;
    
    // 调用底层函数
    lv_obj_t* ret_value = lv_arc_create(parent);

    // 处理返回值
    // 包装为LVGL对象
    jerry_value_t js_obj = jerry_object();
    jerry_value_t ptr = jerry_number((double)(uintptr_t)ret_value);
    jerry_value_t cls = jerry_string_sz("lv_obj");
    
    jerry_object_set(js_obj, jerry_string_sz("__ptr"), ptr);
    jerry_object_set(js_obj, jerry_string_sz("__class"), cls);
    
    jerry_value_free(ptr);
    jerry_value_free(cls);
    return js_obj;
}



/**
 * lv_obj_set_style_text_color function (aliased to lv_obj_set_style_text_color)
 */
static jerry_value_t js_lv_obj_set_style_text_color(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 3) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: value (lv_color_t)
    lv_color_t value = parse_lv_color(args[1]);

    // 解析参数: selector (lv_style_selector_t)
    jerry_value_t js_selector = args[2];
    if (!jerry_value_is_number(js_selector)) {
        return throw_error("Argument 2 must be a number");
    }
    
    uint32_t selector = (uint32_t)jerry_value_as_number(js_selector);
    
    // 调用底层函数
    lv_obj_set_style_text_color(obj, value, selector);
    return jerry_undefined();
}



/**
 * Set the behavior of the label with text longer than the object size
 */
static jerry_value_t js_lv_label_set_long_mode(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 2) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: long_mode (lv_label_long_mode_t)
    jerry_value_t js_long_mode = args[1];
    if (!jerry_value_is_number(js_long_mode)) {
        return throw_error("Argument 1 must be a number");
    }
    
    lv_label_long_mode_t long_mode = (lv_label_long_mode_t)jerry_value_as_number(js_long_mode);
    
    // 调用底层函数
    lv_label_set_long_mode(obj, long_mode);
    return jerry_undefined();
}



/**
 * Create a table object pointer to the created table
 */
static jerry_value_t js_lv_table_create(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: parent (lv_obj_t*)
    jerry_value_t js_parent = args[0];
    if (!jerry_value_is_object(js_parent)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t parent_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t parent_ptr_val = jerry_object_get(js_parent, parent_ptr_prop);
    jerry_value_free(parent_ptr_prop);
    
    if (!jerry_value_is_number(parent_ptr_val)) {
        jerry_value_free(parent_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t parent_ptr = (uintptr_t)jerry_value_as_number(parent_ptr_val);
    jerry_value_free(parent_ptr_val);
    void* parent = (void*)parent_ptr;
    
    // 调用底层函数
    lv_obj_t* ret_value = lv_table_create(parent);

    // 处理返回值
    // 包装为LVGL对象
    jerry_value_t js_obj = jerry_object();
    jerry_value_t ptr = jerry_number((double)(uintptr_t)ret_value);
    jerry_value_t cls = jerry_string_sz("lv_obj");
    
    jerry_object_set(js_obj, jerry_string_sz("__ptr"), ptr);
    jerry_object_set(js_obj, jerry_string_sz("__class"), cls);
    
    jerry_value_free(ptr);
    jerry_value_free(cls);
    return js_obj;
}



/**
 * Create a text area object pointer to the created text area
 */
static jerry_value_t js_lv_textarea_create(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: parent (lv_obj_t*)
    jerry_value_t js_parent = args[0];
    if (!jerry_value_is_object(js_parent)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t parent_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t parent_ptr_val = jerry_object_get(js_parent, parent_ptr_prop);
    jerry_value_free(parent_ptr_prop);
    
    if (!jerry_value_is_number(parent_ptr_val)) {
        jerry_value_free(parent_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t parent_ptr = (uintptr_t)jerry_value_as_number(parent_ptr_val);
    jerry_value_free(parent_ptr_val);
    void* parent = (void*)parent_ptr;
    
    // 调用底层函数
    lv_obj_t* ret_value = lv_textarea_create(parent);

    // 处理返回值
    // 包装为LVGL对象
    jerry_value_t js_obj = jerry_object();
    jerry_value_t ptr = jerry_number((double)(uintptr_t)ret_value);
    jerry_value_t cls = jerry_string_sz("lv_obj");
    
    jerry_object_set(js_obj, jerry_string_sz("__ptr"), ptr);
    jerry_object_set(js_obj, jerry_string_sz("__class"), cls);
    
    jerry_value_free(ptr);
    jerry_value_free(cls);
    return js_obj;
}



/**
 * Set a new value on the slider
 */
static jerry_value_t js_lv_slider_set_value(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 3) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: value (int32_t)
    jerry_value_t js_value = args[1];
    if (!jerry_value_is_number(js_value)) {
        return throw_error("Argument 1 must be a number");
    }
    
    int32_t value = (int32_t)jerry_value_as_number(js_value);
    
    // 解析参数: anim (lv_anim_enable_t)
    // 布尔类型或兼容类型参数: anim
    bool anim = false;
    if (!jerry_value_is_undefined(args[2])) {
        if (jerry_value_is_boolean(args[2])) {
            anim = jerry_value_to_boolean(args[2]);
        }
        else if (jerry_value_is_number(args[2])) {
            anim = (jerry_value_as_number(args[2]) != 0);
        }
        else {
            return throw_error("Argument 2 must be boolean or number for lv_anim_enable_t");
        }
    }
    
    // 调用底层函数
    lv_slider_set_value(obj, value, anim);
    return jerry_undefined();
}



/**
 * Set a new value on the bar
 */
static jerry_value_t js_lv_bar_set_value(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 3) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: value (int32_t)
    jerry_value_t js_value = args[1];
    if (!jerry_value_is_number(js_value)) {
        return throw_error("Argument 1 must be a number");
    }
    
    int32_t value = (int32_t)jerry_value_as_number(js_value);
    
    // 解析参数: anim (lv_anim_enable_t)
    // 布尔类型或兼容类型参数: anim
    bool anim = false;
    if (!jerry_value_is_undefined(args[2])) {
        if (jerry_value_is_boolean(args[2])) {
            anim = jerry_value_to_boolean(args[2]);
        }
        else if (jerry_value_is_number(args[2])) {
            anim = (jerry_value_as_number(args[2]) != 0);
        }
        else {
            return throw_error("Argument 2 must be boolean or number for lv_anim_enable_t");
        }
    }
    
    // 调用底层函数
    lv_bar_set_value(obj, value, anim);
    return jerry_undefined();
}



/**
 * Create a button object pointer to the created button
 */
static jerry_value_t js_lv_btn_create(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: parent (lv_obj_t*)
    jerry_value_t js_parent = args[0];
    if (!jerry_value_is_object(js_parent)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t parent_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t parent_ptr_val = jerry_object_get(js_parent, parent_ptr_prop);
    jerry_value_free(parent_ptr_prop);
    
    if (!jerry_value_is_number(parent_ptr_val)) {
        jerry_value_free(parent_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t parent_ptr = (uintptr_t)jerry_value_as_number(parent_ptr_val);
    jerry_value_free(parent_ptr_val);
    void* parent = (void*)parent_ptr;
    
    // 调用底层函数
    lv_obj_t* ret_value = lv_button_create(parent);

    // 处理返回值
    // 包装为LVGL对象
    jerry_value_t js_obj = jerry_object();
    jerry_value_t ptr = jerry_number((double)(uintptr_t)ret_value);
    jerry_value_t cls = jerry_string_sz("lv_obj");
    
    jerry_object_set(js_obj, jerry_string_sz("__ptr"), ptr);
    jerry_object_set(js_obj, jerry_string_sz("__class"), cls);
    
    jerry_value_free(ptr);
    jerry_value_free(cls);
    return js_obj;
}



/**
 * Enable the recoloring by in-line commands
 */
static jerry_value_t js_lv_label_set_recolor(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 2) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: en (bool)
    // 布尔类型或兼容类型参数: en
    bool en = false;
    if (!jerry_value_is_undefined(args[1])) {
        if (jerry_value_is_boolean(args[1])) {
            en = jerry_value_to_boolean(args[1]);
        }
        else if (jerry_value_is_number(args[1])) {
            en = (jerry_value_as_number(args[1]) != 0);
        }
        else {
            return throw_error("Argument 1 must be boolean or number for bool");
        }
    }
    
    // 调用底层函数
    lv_label_set_recolor(obj, en);
    return jerry_undefined();
}



/**
 * Change the alignment of an object and set new coordinates. Equivalent to: lv_obj_set_align(obj, align); lv_obj_set_pos(obj, x_ofs, y_ofs);
 */
static jerry_value_t js_lv_obj_align(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 4) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: align (lv_align_t)
    jerry_value_t js_align = args[1];
    if (!jerry_value_is_number(js_align)) {
        return throw_error("Argument 1 must be a number");
    }
    
    int align = (int)jerry_value_as_number(js_align);
    
    // 解析参数: x_ofs (int32_t)
    jerry_value_t js_x_ofs = args[2];
    if (!jerry_value_is_number(js_x_ofs)) {
        return throw_error("Argument 2 must be a number");
    }
    
    int32_t x_ofs = (int32_t)jerry_value_as_number(js_x_ofs);
    
    // 解析参数: y_ofs (int32_t)
    jerry_value_t js_y_ofs = args[3];
    if (!jerry_value_is_number(js_y_ofs)) {
        return throw_error("Argument 3 must be a number");
    }
    
    int32_t y_ofs = (int32_t)jerry_value_as_number(js_y_ofs);
    
    // 调用底层函数
    lv_obj_align(obj, align, x_ofs, y_ofs);
    return jerry_undefined();
}



/**
 * Create a chart object pointer to the created chart
 */
static jerry_value_t js_lv_chart_create(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: parent (lv_obj_t*)
    jerry_value_t js_parent = args[0];
    if (!jerry_value_is_object(js_parent)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t parent_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t parent_ptr_val = jerry_object_get(js_parent, parent_ptr_prop);
    jerry_value_free(parent_ptr_prop);
    
    if (!jerry_value_is_number(parent_ptr_val)) {
        jerry_value_free(parent_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t parent_ptr = (uintptr_t)jerry_value_as_number(parent_ptr_val);
    jerry_value_free(parent_ptr_val);
    void* parent = (void*)parent_ptr;
    
    // 调用底层函数
    lv_obj_t* ret_value = lv_chart_create(parent);

    // 处理返回值
    // 包装为LVGL对象
    jerry_value_t js_obj = jerry_object();
    jerry_value_t ptr = jerry_number((double)(uintptr_t)ret_value);
    jerry_value_t cls = jerry_string_sz("lv_obj");
    
    jerry_object_set(js_obj, jerry_string_sz("__ptr"), ptr);
    jerry_object_set(js_obj, jerry_string_sz("__class"), cls);
    
    jerry_value_free(ptr);
    jerry_value_free(cls);
    return js_obj;
}



/**
 * Set the selected option
 */
static jerry_value_t js_lv_roller_set_selected(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 3) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: sel_opt (uint32_t)
    jerry_value_t js_sel_opt = args[1];
    if (!jerry_value_is_number(js_sel_opt)) {
        return throw_error("Argument 1 must be a number");
    }
    
    uint32_t sel_opt = (uint32_t)jerry_value_as_number(js_sel_opt);
    
    // 解析参数: anim (lv_anim_enable_t)
    // 布尔类型或兼容类型参数: anim
    bool anim = false;
    if (!jerry_value_is_undefined(args[2])) {
        if (jerry_value_is_boolean(args[2])) {
            anim = jerry_value_to_boolean(args[2]);
        }
        else if (jerry_value_is_number(args[2])) {
            anim = (jerry_value_as_number(args[2]) != 0);
        }
        else {
            return throw_error("Argument 2 must be boolean or number for lv_anim_enable_t");
        }
    }
    
    // 调用底层函数
    lv_roller_set_selected(obj, sel_opt, anim);
    return jerry_undefined();
}



/**
 * Set the placeholder text of a text area
 */
static jerry_value_t js_lv_textarea_set_placeholder_text(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 2) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: txt (const char*)
    jerry_value_t js_txt = args[1];
    if (!jerry_value_is_string(js_txt)) {
        return throw_error("Argument 1 must be a string");
    }
    
    jerry_size_t txt_len = jerry_string_size(js_txt, JERRY_ENCODING_UTF8);
    char* txt = (char*)malloc(txt_len + 1);
    if (!txt) {
        return throw_error("Out of memory");
    }
    jerry_string_to_buffer(js_txt, JERRY_ENCODING_UTF8, (jerry_char_t*)txt, txt_len);
    txt[txt_len] = '\0';

    // 调用底层函数
    lv_textarea_set_placeholder_text(obj, txt);
    free(txt);
    return jerry_undefined();
}



/**
 * Add a style to an object. lv_obj_add_style(btn, &style_btn, 0); //Default button style lv_obj_add_style(btn, &btn_red, LV_STATE_PRESSED); //Overwrite only some colors to red when pressed
 */
static jerry_value_t js_lv_obj_add_style(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 3) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: style (const lv_style_t*)
    jerry_value_t js_style = args[1];
    if (!jerry_value_is_object(js_style)) {
        return throw_error("Argument 1 must be an object");
    }
    
    jerry_value_t style_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t style_ptr_val = jerry_object_get(js_style, style_ptr_prop);
    jerry_value_free(style_ptr_prop);
    
    if (!jerry_value_is_number(style_ptr_val)) {
        jerry_value_free(style_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t style_ptr = (uintptr_t)jerry_value_as_number(style_ptr_val);
    jerry_value_free(style_ptr_val);
    void* style = (void*)style_ptr;
    
    // 解析参数: selector (lv_style_selector_t)
    jerry_value_t js_selector = args[2];
    if (!jerry_value_is_number(js_selector)) {
        return throw_error("Argument 2 must be a number");
    }
    
    uint32_t selector = (uint32_t)jerry_value_as_number(js_selector);
    
    // 调用底层函数
    lv_obj_add_style(obj, style, selector);
    return jerry_undefined();
}



/**
 * lv_timer_handler function (aliased to lv_timer_handler)
 */
static jerry_value_t js_lv_timer_handler(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 调用底层函数
    uint32_t ret_value = lv_timer_handler();

    // 处理返回值
    return jerry_number(ret_value);
}



/**
 * Insert a text to the current cursor position
 */
static jerry_value_t js_lv_textarea_add_text(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 2) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: txt (const char*)
    jerry_value_t js_txt = args[1];
    if (!jerry_value_is_string(js_txt)) {
        return throw_error("Argument 1 must be a string");
    }
    
    jerry_size_t txt_len = jerry_string_size(js_txt, JERRY_ENCODING_UTF8);
    char* txt = (char*)malloc(txt_len + 1);
    if (!txt) {
        return throw_error("Out of memory");
    }
    jerry_string_to_buffer(js_txt, JERRY_ENCODING_UTF8, (jerry_char_t*)txt, txt_len);
    txt[txt_len] = '\0';

    // 调用底层函数
    lv_textarea_add_text(obj, txt);
    free(txt);
    return jerry_undefined();
}



/**
 * Get the index of the selected option index of the selected option (0 ... number of option - 1);
 */
static jerry_value_t js_lv_dropdown_get_selected(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (const lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 调用底层函数
    uint32_t ret_value = lv_dropdown_get_selected(obj);

    // 处理返回值
    return jerry_number(ret_value);
}



/**
 * Create an empty message box the created message box
 */
static jerry_value_t js_lv_msgbox_create(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: parent (lv_obj_t*)
    jerry_value_t js_parent = args[0];
    if (!jerry_value_is_object(js_parent)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t parent_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t parent_ptr_val = jerry_object_get(js_parent, parent_ptr_prop);
    jerry_value_free(parent_ptr_prop);
    
    if (!jerry_value_is_number(parent_ptr_val)) {
        jerry_value_free(parent_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t parent_ptr = (uintptr_t)jerry_value_as_number(parent_ptr_val);
    jerry_value_free(parent_ptr_val);
    void* parent = (void*)parent_ptr;
    
    // 调用底层函数
    lv_obj_t* ret_value = lv_msgbox_create(parent);

    // 处理返回值
    // 包装为LVGL对象
    jerry_value_t js_obj = jerry_object();
    jerry_value_t ptr = jerry_number((double)(uintptr_t)ret_value);
    jerry_value_t cls = jerry_string_sz("lv_obj");
    
    jerry_object_set(js_obj, jerry_string_sz("__ptr"), ptr);
    jerry_object_set(js_obj, jerry_string_sz("__class"), cls);
    
    jerry_value_free(ptr);
    jerry_value_free(cls);
    return js_obj;
}



/**
 * Remove one or more flags
 */
static jerry_value_t js_lv_obj_clear_flag(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 2) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: f (lv_obj_flag_t)
    jerry_value_t js_f = args[1];
    if (!jerry_value_is_number(js_f)) {
        return throw_error("Argument 1 must be a number");
    }
    
    int f = (int)jerry_value_as_number(js_f);
    
    // 调用底层函数
    lv_obj_remove_flag(obj, f);
    return jerry_undefined();
}



/**
 * Create a roller object pointer to the created roller
 */
static jerry_value_t js_lv_roller_create(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: parent (lv_obj_t*)
    jerry_value_t js_parent = args[0];
    if (!jerry_value_is_object(js_parent)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t parent_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t parent_ptr_val = jerry_object_get(js_parent, parent_ptr_prop);
    jerry_value_free(parent_ptr_prop);
    
    if (!jerry_value_is_number(parent_ptr_val)) {
        jerry_value_free(parent_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t parent_ptr = (uintptr_t)jerry_value_as_number(parent_ptr_val);
    jerry_value_free(parent_ptr_val);
    void* parent = (void*)parent_ptr;
    
    // 调用底层函数
    lv_obj_t* ret_value = lv_roller_create(parent);

    // 处理返回值
    // 包装为LVGL对象
    jerry_value_t js_obj = jerry_object();
    jerry_value_t ptr = jerry_number((double)(uintptr_t)ret_value);
    jerry_value_t cls = jerry_string_sz("lv_obj");
    
    jerry_object_set(js_obj, jerry_string_sz("__ptr"), ptr);
    jerry_object_set(js_obj, jerry_string_sz("__class"), cls);
    
    jerry_value_free(ptr);
    jerry_value_free(cls);
    return js_obj;
}



/**
 * lv_obj_set_style_text_font function (aliased to lv_obj_set_style_text_font)
 */
static jerry_value_t js_lv_obj_set_style_text_font(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 3) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: value (const lv_font_t*)
    jerry_value_t js_value = args[1];
    if (!jerry_value_is_object(js_value)) {
        return throw_error("Argument 1 must be an object");
    }
    
    jerry_value_t value_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t value_ptr_val = jerry_object_get(js_value, value_ptr_prop);
    jerry_value_free(value_ptr_prop);
    
    if (!jerry_value_is_number(value_ptr_val)) {
        jerry_value_free(value_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t value_ptr = (uintptr_t)jerry_value_as_number(value_ptr_val);
    jerry_value_free(value_ptr_val);
    void* value = (void*)value_ptr;
    
    // 解析参数: selector (lv_style_selector_t)
    jerry_value_t js_selector = args[2];
    if (!jerry_value_is_number(js_selector)) {
        return throw_error("Argument 2 must be a number");
    }
    
    uint32_t selector = (uint32_t)jerry_value_as_number(js_selector);
    
    // 调用底层函数
    lv_obj_set_style_text_font(obj, value, selector);
    return jerry_undefined();
}



/**
 * Create a bar object pointer to the created bar
 */
static jerry_value_t js_lv_bar_create(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: parent (lv_obj_t*)
    jerry_value_t js_parent = args[0];
    if (!jerry_value_is_object(js_parent)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t parent_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t parent_ptr_val = jerry_object_get(js_parent, parent_ptr_prop);
    jerry_value_free(parent_ptr_prop);
    
    if (!jerry_value_is_number(parent_ptr_val)) {
        jerry_value_free(parent_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t parent_ptr = (uintptr_t)jerry_value_as_number(parent_ptr_val);
    jerry_value_free(parent_ptr_val);
    void* parent = (void*)parent_ptr;
    
    // 调用底层函数
    lv_obj_t* ret_value = lv_bar_create(parent);

    // 处理返回值
    // 包装为LVGL对象
    jerry_value_t js_obj = jerry_object();
    jerry_value_t ptr = jerry_number((double)(uintptr_t)ret_value);
    jerry_value_t cls = jerry_string_sz("lv_obj");
    
    jerry_object_set(js_obj, jerry_string_sz("__ptr"), ptr);
    jerry_object_set(js_obj, jerry_string_sz("__class"), cls);
    
    jerry_value_free(ptr);
    jerry_value_free(cls);
    return js_obj;
}



/**
 * Set the options in a drop-down list from a string. The options will be copied and saved in the object so the options can be destroyed after calling this function
 */
static jerry_value_t js_lv_dropdown_set_options(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 2) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: options (const char*)
    jerry_value_t js_options = args[1];
    if (!jerry_value_is_string(js_options)) {
        return throw_error("Argument 1 must be a string");
    }
    
    jerry_size_t options_len = jerry_string_size(js_options, JERRY_ENCODING_UTF8);
    char* options = (char*)malloc(options_len + 1);
    if (!options) {
        return throw_error("Out of memory");
    }
    jerry_string_to_buffer(js_options, JERRY_ENCODING_UTF8, (jerry_char_t*)options, options_len);
    options[options_len] = '\0';

    // 调用底层函数
    lv_dropdown_set_options(obj, options);
    free(options);
    return jerry_undefined();
}



/**
 * Get the value of the main knob of a slider the value of the main knob of the slider
 */
static jerry_value_t js_lv_slider_get_value(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (const lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 调用底层函数
    int32_t ret_value = lv_slider_get_value(obj);

    // 处理返回值
    return jerry_number(ret_value);
}



/**
 * Set the number of columns
 */
static jerry_value_t js_lv_table_set_col_cnt(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 2) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: col_cnt (uint32_t)
    jerry_value_t js_col_cnt = args[1];
    if (!jerry_value_is_number(js_col_cnt)) {
        return throw_error("Argument 1 must be a number");
    }
    
    uint32_t col_cnt = (uint32_t)jerry_value_as_number(js_col_cnt);
    
    // 调用底层函数
    lv_table_set_column_count(obj, col_cnt);
    return jerry_undefined();
}



/**
 * lv_obj_set_style_border_width function (aliased to lv_obj_set_style_border_width)
 */
static jerry_value_t js_lv_obj_set_style_border_width(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 3) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: value (int32_t)
    jerry_value_t js_value = args[1];
    if (!jerry_value_is_number(js_value)) {
        return throw_error("Argument 1 must be a number");
    }
    
    int32_t value = (int32_t)jerry_value_as_number(js_value);
    
    // 解析参数: selector (lv_style_selector_t)
    jerry_value_t js_selector = args[2];
    if (!jerry_value_is_number(js_selector)) {
        return throw_error("Argument 2 must be a number");
    }
    
    uint32_t selector = (uint32_t)jerry_value_as_number(js_selector);
    
    // 调用底层函数
    lv_obj_set_style_border_width(obj, value, selector);
    return jerry_undefined();
}



/**
 * Set the options on a roller
 */
static jerry_value_t js_lv_roller_set_options(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 3) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: options (const char*)
    jerry_value_t js_options = args[1];
    if (!jerry_value_is_string(js_options)) {
        return throw_error("Argument 1 must be a string");
    }
    
    jerry_size_t options_len = jerry_string_size(js_options, JERRY_ENCODING_UTF8);
    char* options = (char*)malloc(options_len + 1);
    if (!options) {
        return throw_error("Out of memory");
    }
    jerry_string_to_buffer(js_options, JERRY_ENCODING_UTF8, (jerry_char_t*)options, options_len);
    options[options_len] = '\0';

    // 解析参数: mode (lv_roller_mode_t)
    jerry_value_t js_mode = args[2];
    if (!jerry_value_is_number(js_mode)) {
        return throw_error("Argument 2 must be a number");
    }
    
    int mode = (int)jerry_value_as_number(js_mode);
    
    // 调用底层函数
    lv_roller_set_options(obj, options, mode);
    free(options);
    return jerry_undefined();
}



/**
 * Create a switch object pointer to the created switch
 */
static jerry_value_t js_lv_switch_create(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: parent (lv_obj_t*)
    jerry_value_t js_parent = args[0];
    if (!jerry_value_is_object(js_parent)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t parent_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t parent_ptr_val = jerry_object_get(js_parent, parent_ptr_prop);
    jerry_value_free(parent_ptr_prop);
    
    if (!jerry_value_is_number(parent_ptr_val)) {
        jerry_value_free(parent_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t parent_ptr = (uintptr_t)jerry_value_as_number(parent_ptr_val);
    jerry_value_free(parent_ptr_val);
    void* parent = (void*)parent_ptr;
    
    // 调用底层函数
    lv_obj_t* ret_value = lv_switch_create(parent);

    // 处理返回值
    // 包装为LVGL对象
    jerry_value_t js_obj = jerry_object();
    jerry_value_t ptr = jerry_number((double)(uintptr_t)ret_value);
    jerry_value_t cls = jerry_string_sz("lv_obj");
    
    jerry_object_set(js_obj, jerry_string_sz("__ptr"), ptr);
    jerry_object_set(js_obj, jerry_string_sz("__class"), cls);
    
    jerry_value_free(ptr);
    jerry_value_free(cls);
    return js_obj;
}



/**
 * Set the width of an object possible values are: pixel simple set the size accordingly LV_SIZE_CONTENT set the size to involve all children in the given direction lv_pct(x) to set size in percentage of the parent's content area size (the size without paddings). x should be in [0..1000]% range
 */
static jerry_value_t js_lv_obj_set_width(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 2) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: w (int32_t)
    jerry_value_t js_w = args[1];
    if (!jerry_value_is_number(js_w)) {
        return throw_error("Argument 1 must be a number");
    }
    
    int32_t w = (int32_t)jerry_value_as_number(js_w);
    
    // 调用底层函数
    lv_obj_set_width(obj, w);
    return jerry_undefined();
}



/**
 * Get the active screen of the default display pointer to the active screen
 */
static jerry_value_t js_lv_scr_act(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 调用底层函数
    lv_obj_t* ret_value = lv_screen_active();

    // 处理返回值
    // 包装为LVGL对象
    jerry_value_t js_obj = jerry_object();
    jerry_value_t ptr = jerry_number((double)(uintptr_t)ret_value);
    jerry_value_t cls = jerry_string_sz("lv_obj");
    
    jerry_object_set(js_obj, jerry_string_sz("__ptr"), ptr);
    jerry_object_set(js_obj, jerry_string_sz("__class"), cls);
    
    jerry_value_free(ptr);
    jerry_value_free(cls);
    return js_obj;
}



/**
 * Set a new type for a chart
 */
static jerry_value_t js_lv_chart_set_type(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 2) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: type (lv_chart_type_t)
    jerry_value_t js_type = args[1];
    if (!jerry_value_is_number(js_type)) {
        return throw_error("Argument 1 must be a number");
    }
    
    int type = (int)jerry_value_as_number(js_type);
    
    // 调用底层函数
    lv_chart_set_type(obj, type);
    return jerry_undefined();
}



/**
 * Align an object to the center on its parent. if the parent size changes obj needs to be aligned manually again
 */
static jerry_value_t js_lv_obj_center(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 调用底层函数
    lv_obj_center(obj);
    return jerry_undefined();
}



/**
 * Delay for the given milliseconds. By default it's a blocking delay, but with :ref:`lv_delay_set_cb()` a custom delay function can be set too
 */
static jerry_value_t js_lv_delay_ms(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: ms (uint32_t)
    jerry_value_t js_ms = args[0];
    if (!jerry_value_is_number(js_ms)) {
        return throw_error("Argument 0 must be a number");
    }
    
    uint32_t ms = (uint32_t)jerry_value_as_number(js_ms);
    
    // 调用底层函数
    lv_delay_ms(ms);
    return jerry_undefined();
}



/**
 * Create an image object pointer to the created image
 */
static jerry_value_t js_lv_img_create(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: parent (lv_obj_t*)
    jerry_value_t js_parent = args[0];
    if (!jerry_value_is_object(js_parent)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t parent_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t parent_ptr_val = jerry_object_get(js_parent, parent_ptr_prop);
    jerry_value_free(parent_ptr_prop);
    
    if (!jerry_value_is_number(parent_ptr_val)) {
        jerry_value_free(parent_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t parent_ptr = (uintptr_t)jerry_value_as_number(parent_ptr_val);
    jerry_value_free(parent_ptr_val);
    void* parent = (void*)parent_ptr;
    
    // 调用底层函数
    lv_obj_t* ret_value = lv_image_create(parent);

    // 处理返回值
    // 包装为LVGL对象
    jerry_value_t js_obj = jerry_object();
    jerry_value_t ptr = jerry_number((double)(uintptr_t)ret_value);
    jerry_value_t cls = jerry_string_sz("lv_obj");
    
    jerry_object_set(js_obj, jerry_string_sz("__ptr"), ptr);
    jerry_object_set(js_obj, jerry_string_sz("__class"), cls);
    
    jerry_value_free(ptr);
    jerry_value_free(cls);
    return js_obj;
}



/**
 * lv_obj_set_style_pad_column function (aliased to lv_obj_set_style_pad_column)
 */
static jerry_value_t js_lv_obj_set_style_pad_column(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 3) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: value (int32_t)
    jerry_value_t js_value = args[1];
    if (!jerry_value_is_number(js_value)) {
        return throw_error("Argument 1 must be a number");
    }
    
    int32_t value = (int32_t)jerry_value_as_number(js_value);
    
    // 解析参数: selector (lv_style_selector_t)
    jerry_value_t js_selector = args[2];
    if (!jerry_value_is_number(js_selector)) {
        return throw_error("Argument 2 must be a number");
    }
    
    uint32_t selector = (uint32_t)jerry_value_as_number(js_selector);
    
    // 调用底层函数
    lv_obj_set_style_pad_column(obj, value, selector);
    return jerry_undefined();
}



/**
 * Set the selected option
 */
static jerry_value_t js_lv_dropdown_set_selected(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 3) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: sel_opt (uint32_t)
    jerry_value_t js_sel_opt = args[1];
    if (!jerry_value_is_number(js_sel_opt)) {
        return throw_error("Argument 1 must be a number");
    }
    
    uint32_t sel_opt = (uint32_t)jerry_value_as_number(js_sel_opt);
    
    // 解析参数: anim (lv_anim_enable_t)
    // 布尔类型或兼容类型参数: anim
    bool anim = false;
    if (!jerry_value_is_undefined(args[2])) {
        if (jerry_value_is_boolean(args[2])) {
            anim = jerry_value_to_boolean(args[2]);
        }
        else if (jerry_value_is_number(args[2])) {
            anim = (jerry_value_as_number(args[2]) != 0);
        }
        else {
            return throw_error("Argument 2 must be boolean or number for lv_anim_enable_t");
        }
    }
    
    // 调用底层函数
    lv_dropdown_set_selected(obj, sel_opt, anim);
    return jerry_undefined();
}



/**
 * Set the number of points on a data line on a chart
 */
static jerry_value_t js_lv_chart_set_point_count(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 2) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: cnt (uint32_t)
    jerry_value_t js_cnt = args[1];
    if (!jerry_value_is_number(js_cnt)) {
        return throw_error("Argument 1 must be a number");
    }
    
    uint32_t cnt = (uint32_t)jerry_value_as_number(js_cnt);
    
    // 调用底层函数
    lv_chart_set_point_count(obj, cnt);
    return jerry_undefined();
}



/**
 * Set the rotation angle of the image. The image will be rotated around the set pivot set by :ref:`lv_image_set_pivot()` Note that indexed and alpha only images can't be transformed. if image_align is LV_IMAGE_ALIGN_STRETCH or LV_IMAGE_ALIGN_FIT rotation will be set to 0 automatically.
 */
static jerry_value_t js_lv_img_set_angle(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 2) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 解析参数: angle (int32_t)
    jerry_value_t js_angle = args[1];
    if (!jerry_value_is_number(js_angle)) {
        return throw_error("Argument 1 must be a number");
    }
    
    int32_t angle = (int32_t)jerry_value_as_number(js_angle);
    
    // 调用底层函数
    lv_image_set_rotation(obj, angle);
    return jerry_undefined();
}



/**
 * Get the text of a label the text of the label
 */
static jerry_value_t js_lv_label_get_text(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数数量检查
    if (argc < 1) {
        return throw_error("Insufficient arguments");
    }

    // 解析参数: obj (const lv_obj_t*)
    jerry_value_t js_obj = args[0];
    if (!jerry_value_is_object(js_obj)) {
        return throw_error("Argument 0 must be an object");
    }
    
    jerry_value_t obj_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t obj_ptr_val = jerry_object_get(js_obj, obj_ptr_prop);
    jerry_value_free(obj_ptr_prop);
    
    if (!jerry_value_is_number(obj_ptr_val)) {
        jerry_value_free(obj_ptr_val);
        return throw_error("Invalid __ptr property");
    }
    
    uintptr_t obj_ptr = (uintptr_t)jerry_value_as_number(obj_ptr_val);
    jerry_value_free(obj_ptr_val);
    void* obj = (void*)obj_ptr;
    
    // 调用底层函数
    char* ret_value = lv_label_get_text(obj);

    // 处理返回值
    if (ret_value == NULL) {
        return jerry_string_sz("");
    }
    return jerry_string_sz((const jerry_char_t*)ret_value);
}



const AppSysFuncEntry lvgl_binding_funcs[] = {
    { "register_lv_event_handler", register_lv_event_handler },
    { "unregister_lv_event_handler", unregister_lv_event_handler },
    { "lv_label_set_text", js_lv_label_set_text },
    { "lv_obj_add_flag", js_lv_obj_add_flag },
    { "lv_obj_set_pos", js_lv_obj_set_pos },
    { "lv_bar_set_range", js_lv_bar_set_range },
    { "lv_checkbox_create", js_lv_checkbox_create },
    { "lv_slider_create", js_lv_slider_create },
    { "lv_obj_create", js_lv_obj_create },
    { "lv_obj_set_state", js_lv_obj_set_state },
    { "lv_chart_set_range", js_lv_chart_set_range },
    { "lv_obj_set_style_radius", js_lv_obj_set_style_radius },
    { "lv_obj_clean", js_lv_obj_clean },
    { "lv_slider_set_range", js_lv_slider_set_range },
    { "lv_textarea_set_text", js_lv_textarea_set_text },
    { "lv_event_get_target", js_lv_event_get_target },
    { "lv_msgbox_close", js_lv_msgbox_close },
    { "lv_obj_set_style_pad_row", js_lv_obj_set_style_pad_row },
    { "lv_event_get_user_data", js_lv_event_get_user_data },
    { "lv_arc_set_range", js_lv_arc_set_range },
    { "lv_obj_set_style_pad_all", js_lv_obj_set_style_pad_all },
    { "lv_obj_set_style_border_color", js_lv_obj_set_style_border_color },
    { "lv_obj_set_style_bg_color", js_lv_obj_set_style_bg_color },
    { "lv_arc_set_value", js_lv_arc_set_value },
    { "lv_label_create", js_lv_label_create },
    { "lv_dropdown_create", js_lv_dropdown_create },
    { "lv_obj_align_to", js_lv_obj_align_to },
    { "lv_table_set_row_cnt", js_lv_table_set_row_cnt },
    { "lv_obj_del", js_lv_obj_del },
    { "lv_checkbox_set_text", js_lv_checkbox_set_text },
    { "lv_img_set_zoom", js_lv_img_set_zoom },
    { "lv_event_get_code", js_lv_event_get_code },
    { "lv_arc_set_bg_angles", js_lv_arc_set_bg_angles },
    { "lv_table_set_cell_value", js_lv_table_set_cell_value },
    { "lv_obj_set_size", js_lv_obj_set_size },
    { "lv_arc_create", js_lv_arc_create },
    { "lv_obj_set_style_text_color", js_lv_obj_set_style_text_color },
    { "lv_label_set_long_mode", js_lv_label_set_long_mode },
    { "lv_table_create", js_lv_table_create },
    { "lv_textarea_create", js_lv_textarea_create },
    { "lv_slider_set_value", js_lv_slider_set_value },
    { "lv_bar_set_value", js_lv_bar_set_value },
    { "lv_btn_create", js_lv_btn_create },
    { "lv_label_set_recolor", js_lv_label_set_recolor },
    { "lv_obj_align", js_lv_obj_align },
    { "lv_chart_create", js_lv_chart_create },
    { "lv_roller_set_selected", js_lv_roller_set_selected },
    { "lv_textarea_set_placeholder_text", js_lv_textarea_set_placeholder_text },
    { "lv_obj_add_style", js_lv_obj_add_style },
    { "lv_timer_handler", js_lv_timer_handler },
    { "lv_textarea_add_text", js_lv_textarea_add_text },
    { "lv_dropdown_get_selected", js_lv_dropdown_get_selected },
    { "lv_msgbox_create", js_lv_msgbox_create },
    { "lv_obj_clear_flag", js_lv_obj_clear_flag },
    { "lv_roller_create", js_lv_roller_create },
    { "lv_obj_set_style_text_font", js_lv_obj_set_style_text_font },
    { "lv_bar_create", js_lv_bar_create },
    { "lv_dropdown_set_options", js_lv_dropdown_set_options },
    { "lv_slider_get_value", js_lv_slider_get_value },
    { "lv_table_set_col_cnt", js_lv_table_set_col_cnt },
    { "lv_obj_set_style_border_width", js_lv_obj_set_style_border_width },
    { "lv_roller_set_options", js_lv_roller_set_options },
    { "lv_switch_create", js_lv_switch_create },
    { "lv_obj_set_width", js_lv_obj_set_width },
    { "lv_scr_act", js_lv_scr_act },
    { "lv_chart_set_type", js_lv_chart_set_type },
    { "lv_obj_center", js_lv_obj_center },
    { "lv_delay_ms", js_lv_delay_ms },
    { "lv_img_create", js_lv_img_create },
    { "lv_obj_set_style_pad_column", js_lv_obj_set_style_pad_column },
    { "lv_dropdown_set_selected", js_lv_dropdown_set_selected },
    { "lv_chart_set_point_count", js_lv_chart_set_point_count },
    { "lv_img_set_angle", js_lv_img_set_angle },
    { "lv_label_get_text", js_lv_label_get_text }
};

const unsigned int lvgl_binding_funcs_count = 74;

static void lvgl_binding_set_enum(jerry_value_t obj, const char* key, int32_t val) {
    jerry_value_t jkey = jerry_string_sz(key);
    jerry_value_t jval = jerry_number(val);
    jerry_object_set(obj, jkey, jval);
    jerry_value_free(jkey);
    jerry_value_free(jval);
}

void register_lvgl_enums(void) {
    jerry_value_t lvgl_enum_obj = jerry_object();
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_RESULT_INVALID", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_RESULT_OK", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_RB_COLOR_RED", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_RB_COLOR_BLACK", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ALIGN_DEFAULT", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ALIGN_TOP_LEFT", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ALIGN_TOP_MID", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ALIGN_TOP_RIGHT", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ALIGN_BOTTOM_LEFT", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ALIGN_BOTTOM_MID", 5);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ALIGN_BOTTOM_RIGHT", 6);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ALIGN_LEFT_MID", 7);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ALIGN_RIGHT_MID", 8);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ALIGN_CENTER", 9);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ALIGN_OUT_TOP_LEFT", 10);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ALIGN_OUT_TOP_MID", 11);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ALIGN_OUT_TOP_RIGHT", 12);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ALIGN_OUT_BOTTOM_LEFT", 13);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ALIGN_OUT_BOTTOM_MID", 14);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ALIGN_OUT_BOTTOM_RIGHT", 15);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ALIGN_OUT_LEFT_TOP", 16);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ALIGN_OUT_LEFT_MID", 17);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ALIGN_OUT_LEFT_BOTTOM", 18);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ALIGN_OUT_RIGHT_TOP", 19);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ALIGN_OUT_RIGHT_MID", 20);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ALIGN_OUT_RIGHT_BOTTOM", 21);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DIR_NONE", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DIR_LEFT", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DIR_RIGHT", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DIR_TOP", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DIR_BOTTOM", 8);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DIR_HOR", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DIR_VER", 12);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DIR_ALL", 15);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OPA_TRANSP", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OPA_0", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OPA_10", 25);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OPA_20", 51);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OPA_30", 76);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OPA_40", 102);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OPA_50", 127);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OPA_60", 153);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OPA_70", 178);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OPA_80", 204);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OPA_90", 229);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OPA_100", 255);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OPA_COVER", 255);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_UNKNOWN", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_RAW", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_RAW_ALPHA", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_L8", 6);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_I1", 7);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_I2", 8);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_I4", 9);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_I8", 10);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_A8", 14);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_RGB565", 18);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_ARGB8565", 19);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_RGB565A8", 20);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_AL88", 21);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_RGB888", 15);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_ARGB8888", 16);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_XRGB8888", 17);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_A1", 11);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_A2", 12);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_A4", 13);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_ARGB1555", 22);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_ARGB4444", 23);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_ARGB2222", 24);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_YUV_START", 32);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_I420", 32);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_I422", 33);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_I444", 34);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_I400", 35);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_NV21", 36);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_NV12", 37);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_YUY2", 38);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_UYVY", 39);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_YUV_END", 39);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_PROPRIETARY_START", 48);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_NEMA_TSC_START", 48);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_NEMA_TSC4", 48);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_NEMA_TSC6", 49);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_NEMA_TSC6A", 50);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_NEMA_TSC6AP", 51);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_NEMA_TSC12", 52);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_NEMA_TSC12A", 53);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_NEMA_TSC_END", 53);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_NATIVE", 18);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COLOR_FORMAT_NATIVE_WITH_ALPHA", 20);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PALETTE_RED", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PALETTE_PINK", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PALETTE_PURPLE", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PALETTE_DEEP_PURPLE", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PALETTE_INDIGO", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PALETTE_BLUE", 5);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PALETTE_LIGHT_BLUE", 6);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PALETTE_CYAN", 7);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PALETTE_TEAL", 8);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PALETTE_GREEN", 9);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PALETTE_LIGHT_GREEN", 10);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PALETTE_LIME", 11);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PALETTE_YELLOW", 12);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PALETTE_AMBER", 13);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PALETTE_ORANGE", 14);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PALETTE_DEEP_ORANGE", 15);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PALETTE_BROWN", 16);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PALETTE_BLUE_GREY", 17);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PALETTE_GREY", 18);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PALETTE_LAST", 19);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PALETTE_NONE", 255);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_COMPRESS_NONE", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_COMPRESS_RLE", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_COMPRESS_LZ4", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TREE_WALK_PRE_ORDER", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TREE_WALK_POST_ORDER", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_BULLET", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_AUDIO", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_VIDEO", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_LIST", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_OK", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_CLOSE", 5);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_POWER", 6);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_SETTINGS", 7);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_HOME", 8);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_DOWNLOAD", 9);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_DRIVE", 10);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_REFRESH", 11);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_MUTE", 12);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_VOLUME_MID", 13);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_VOLUME_MAX", 14);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_IMAGE", 15);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_TINT", 16);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_PREV", 17);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_PLAY", 18);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_PAUSE", 19);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_STOP", 20);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_NEXT", 21);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_EJECT", 22);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_LEFT", 23);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_RIGHT", 24);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_PLUS", 25);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_MINUS", 26);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_EYE_OPEN", 27);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_EYE_CLOSE", 28);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_WARNING", 29);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_SHUFFLE", 30);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_UP", 31);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_DOWN", 32);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_LOOP", 33);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_DIRECTORY", 34);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_UPLOAD", 35);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_CALL", 36);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_CUT", 37);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_COPY", 38);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_SAVE", 39);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_BARS", 40);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_ENVELOPE", 41);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_CHARGE", 42);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_PASTE", 43);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_BELL", 44);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_KEYBOARD", 45);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_GPS", 46);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_FILE", 47);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_WIFI", 48);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_BATTERY_FULL", 49);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_BATTERY_3", 50);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_BATTERY_2", 51);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_BATTERY_1", 52);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_BATTERY_EMPTY", 53);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_USB", 54);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_BLUETOOTH", 55);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_TRASH", 56);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_EDIT", 57);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_BACKSPACE", 58);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_SD_CARD", 59);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_NEW_LINE", 60);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STR_SYMBOL_DUMMY", 61);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_GLYPH_FORMAT_NONE", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_GLYPH_FORMAT_A1", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_GLYPH_FORMAT_A2", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_GLYPH_FORMAT_A3", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_GLYPH_FORMAT_A4", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_GLYPH_FORMAT_A8", 8);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_GLYPH_FORMAT_A1_ALIGNED", 17);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_GLYPH_FORMAT_A2_ALIGNED", 18);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_GLYPH_FORMAT_A4_ALIGNED", 20);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_GLYPH_FORMAT_A8_ALIGNED", 24);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_GLYPH_FORMAT_IMAGE", 25);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_GLYPH_FORMAT_VECTOR", 26);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_GLYPH_FORMAT_SVG", 27);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_GLYPH_FORMAT_CUSTOM", 255);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_SUBPX_NONE", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_SUBPX_HOR", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_SUBPX_VER", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_SUBPX_BOTH", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_KERNING_NORMAL", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_KERNING_NONE", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TEXT_FLAG_NONE", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TEXT_FLAG_EXPAND", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TEXT_FLAG_FIT", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TEXT_FLAG_BREAK_ALL", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TEXT_FLAG_RECOLOR", 8);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TEXT_ALIGN_AUTO", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TEXT_ALIGN_LEFT", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TEXT_ALIGN_CENTER", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TEXT_ALIGN_RIGHT", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TEXT_CMD_STATE_WAIT", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TEXT_CMD_STATE_PAR", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TEXT_CMD_STATE_IN", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BASE_DIR_LTR", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BASE_DIR_RTL", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BASE_DIR_AUTO", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BASE_DIR_NEUTRAL", 32);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BASE_DIR_WEAK", 33);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_GRAD_DIR_NONE", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_GRAD_DIR_VER", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_GRAD_DIR_HOR", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_GRAD_DIR_LINEAR", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_GRAD_DIR_RADIAL", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_GRAD_DIR_CONICAL", 5);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_GRAD_EXTEND_PAD", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_GRAD_EXTEND_REPEAT", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_GRAD_EXTEND_REFLECT", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_LAYOUT_NONE", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_LAYOUT_FLEX", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_LAYOUT_GRID", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_LAYOUT_LAST", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FLEX_ALIGN_START", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FLEX_ALIGN_END", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FLEX_ALIGN_CENTER", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FLEX_ALIGN_SPACE_EVENLY", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FLEX_ALIGN_SPACE_AROUND", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FLEX_ALIGN_SPACE_BETWEEN", 5);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FLEX_FLOW_ROW", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FLEX_FLOW_COLUMN", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FLEX_FLOW_ROW_WRAP", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FLEX_FLOW_ROW_REVERSE", 8);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FLEX_FLOW_ROW_WRAP_REVERSE", 12);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FLEX_FLOW_COLUMN_WRAP", 5);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FLEX_FLOW_COLUMN_REVERSE", 9);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FLEX_FLOW_COLUMN_WRAP_REVERSE", 13);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_GRID_ALIGN_START", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_GRID_ALIGN_CENTER", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_GRID_ALIGN_END", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_GRID_ALIGN_STRETCH", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_GRID_ALIGN_SPACE_EVENLY", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_GRID_ALIGN_SPACE_AROUND", 5);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_GRID_ALIGN_SPACE_BETWEEN", 6);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BLEND_MODE_NORMAL", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BLEND_MODE_ADDITIVE", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BLEND_MODE_SUBTRACTIVE", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BLEND_MODE_MULTIPLY", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BLEND_MODE_DIFFERENCE", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TEXT_DECOR_NONE", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TEXT_DECOR_UNDERLINE", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TEXT_DECOR_STRIKETHROUGH", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BORDER_SIDE_NONE", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BORDER_SIDE_BOTTOM", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BORDER_SIDE_TOP", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BORDER_SIDE_LEFT", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BORDER_SIDE_RIGHT", 8);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BORDER_SIDE_FULL", 15);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BORDER_SIDE_INTERNAL", 16);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_PROP_INV", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_WIDTH", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_HEIGHT", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_LENGTH", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_MIN_WIDTH", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_MAX_WIDTH", 5);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_MIN_HEIGHT", 6);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_MAX_HEIGHT", 7);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_X", 8);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_Y", 9);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_ALIGN", 10);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_RADIUS", 12);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_RADIAL_OFFSET", 13);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_PAD_RADIAL", 14);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_PAD_TOP", 16);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_PAD_BOTTOM", 17);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_PAD_LEFT", 18);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_PAD_RIGHT", 19);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_PAD_ROW", 20);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_PAD_COLUMN", 21);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_LAYOUT", 22);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_MARGIN_TOP", 24);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_MARGIN_BOTTOM", 25);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_MARGIN_LEFT", 26);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_MARGIN_RIGHT", 27);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_BG_COLOR", 28);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_BG_OPA", 29);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_BG_GRAD_DIR", 32);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_BG_MAIN_STOP", 33);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_BG_GRAD_STOP", 34);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_BG_GRAD_COLOR", 35);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_BG_MAIN_OPA", 36);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_BG_GRAD_OPA", 37);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_BG_GRAD", 38);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_BASE_DIR", 39);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_BG_IMAGE_SRC", 40);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_BG_IMAGE_OPA", 41);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_BG_IMAGE_RECOLOR", 42);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_BG_IMAGE_RECOLOR_OPA", 43);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_BG_IMAGE_TILED", 44);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_CLIP_CORNER", 45);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_BORDER_WIDTH", 48);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_BORDER_COLOR", 49);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_BORDER_OPA", 50);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_BORDER_SIDE", 52);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_BORDER_POST", 53);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_OUTLINE_WIDTH", 56);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_OUTLINE_COLOR", 57);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_OUTLINE_OPA", 58);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_OUTLINE_PAD", 59);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_SHADOW_WIDTH", 60);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_SHADOW_COLOR", 61);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_SHADOW_OPA", 62);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_SHADOW_OFFSET_X", 64);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_SHADOW_OFFSET_Y", 65);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_SHADOW_SPREAD", 66);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_IMAGE_OPA", 68);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_IMAGE_RECOLOR", 69);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_IMAGE_RECOLOR_OPA", 70);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_LINE_WIDTH", 72);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_LINE_DASH_WIDTH", 73);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_LINE_DASH_GAP", 74);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_LINE_ROUNDED", 75);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_LINE_COLOR", 76);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_LINE_OPA", 77);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_ARC_WIDTH", 80);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_ARC_ROUNDED", 81);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_ARC_COLOR", 82);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_ARC_OPA", 83);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_ARC_IMAGE_SRC", 84);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TEXT_COLOR", 88);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TEXT_OPA", 89);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TEXT_FONT", 90);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TEXT_LETTER_SPACE", 91);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TEXT_LINE_SPACE", 92);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TEXT_DECOR", 93);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TEXT_ALIGN", 94);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TEXT_OUTLINE_STROKE_WIDTH", 95);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TEXT_OUTLINE_STROKE_OPA", 96);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TEXT_OUTLINE_STROKE_COLOR", 97);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_OPA", 98);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_OPA_LAYERED", 99);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_COLOR_FILTER_DSC", 100);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_COLOR_FILTER_OPA", 101);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_ANIM", 102);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_ANIM_DURATION", 103);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TRANSITION", 104);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_BLEND_MODE", 105);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TRANSFORM_WIDTH", 106);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TRANSFORM_HEIGHT", 107);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TRANSLATE_X", 108);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TRANSLATE_Y", 109);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TRANSFORM_SCALE_X", 110);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TRANSFORM_SCALE_Y", 111);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TRANSFORM_ROTATION", 112);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TRANSFORM_PIVOT_X", 113);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TRANSFORM_PIVOT_Y", 114);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TRANSFORM_SKEW_X", 115);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TRANSFORM_SKEW_Y", 116);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_BITMAP_MASK_SRC", 117);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_ROTARY_SENSITIVITY", 118);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TRANSLATE_RADIAL", 119);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_RECOLOR", 120);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_RECOLOR_OPA", 121);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_FLEX_FLOW", 122);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_FLEX_MAIN_PLACE", 123);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_FLEX_CROSS_PLACE", 124);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_FLEX_TRACK_PLACE", 125);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_FLEX_GROW", 126);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_GRID_COLUMN_ALIGN", 127);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_GRID_ROW_ALIGN", 128);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_GRID_ROW_DSC_ARRAY", 129);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_GRID_COLUMN_DSC_ARRAY", 130);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_GRID_CELL_COLUMN_POS", 131);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_GRID_CELL_COLUMN_SPAN", 132);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_GRID_CELL_X_ALIGN", 133);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_GRID_CELL_ROW_POS", 134);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_GRID_CELL_ROW_SPAN", 135);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_GRID_CELL_Y_ALIGN", 136);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_LAST_BUILT_IN_PROP", 137);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_NUM_BUILT_IN_PROPS", 138);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_PROP_ANY", 255);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_PROP_CONST", 255);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_RES_NOT_FOUND", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_RES_FOUND", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_ALL", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_PRESSED", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_PRESSING", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_PRESS_LOST", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_SHORT_CLICKED", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_SINGLE_CLICKED", 5);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_DOUBLE_CLICKED", 6);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_TRIPLE_CLICKED", 7);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_LONG_PRESSED", 8);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_LONG_PRESSED_REPEAT", 9);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_CLICKED", 10);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_RELEASED", 11);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_SCROLL_BEGIN", 12);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_SCROLL_THROW_BEGIN", 13);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_SCROLL_END", 14);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_SCROLL", 15);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_GESTURE", 16);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_KEY", 17);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_ROTARY", 18);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_FOCUSED", 19);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_DEFOCUSED", 20);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_LEAVE", 21);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_HIT_TEST", 22);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_INDEV_RESET", 23);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_HOVER_OVER", 24);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_HOVER_LEAVE", 25);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_COVER_CHECK", 26);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_REFR_EXT_DRAW_SIZE", 27);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_DRAW_MAIN_BEGIN", 28);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_DRAW_MAIN", 29);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_DRAW_MAIN_END", 30);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_DRAW_POST_BEGIN", 31);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_DRAW_POST", 32);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_DRAW_POST_END", 33);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_DRAW_TASK_ADDED", 34);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_VALUE_CHANGED", 35);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_INSERT", 36);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_REFRESH", 37);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_READY", 38);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_CANCEL", 39);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_CREATE", 40);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_DELETE", 41);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_CHILD_CHANGED", 42);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_CHILD_CREATED", 43);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_CHILD_DELETED", 44);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_SCREEN_UNLOAD_START", 45);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_SCREEN_LOAD_START", 46);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_SCREEN_LOADED", 47);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_SCREEN_UNLOADED", 48);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_SIZE_CHANGED", 49);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_STYLE_CHANGED", 50);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_LAYOUT_CHANGED", 51);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_GET_SELF_SIZE", 52);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_INVALIDATE_AREA", 53);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_RESOLUTION_CHANGED", 54);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_COLOR_FORMAT_CHANGED", 55);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_REFR_REQUEST", 56);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_REFR_START", 57);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_REFR_READY", 58);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_RENDER_START", 59);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_RENDER_READY", 60);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_FLUSH_START", 61);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_FLUSH_FINISH", 62);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_FLUSH_WAIT_START", 63);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_FLUSH_WAIT_FINISH", 64);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_VSYNC", 65);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_LAST", 66);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_PREPROCESS", 32768);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_EVENT_MARKED_DELETING", 65536);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DISPLAY_ROTATION_0", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DISPLAY_ROTATION_90", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DISPLAY_ROTATION_180", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DISPLAY_ROTATION_270", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DISPLAY_RENDER_MODE_PARTIAL", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DISPLAY_RENDER_MODE_DIRECT", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DISPLAY_RENDER_MODE_FULL", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCR_LOAD_ANIM_NONE", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCR_LOAD_ANIM_OVER_LEFT", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCR_LOAD_ANIM_OVER_RIGHT", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCR_LOAD_ANIM_OVER_TOP", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCR_LOAD_ANIM_OVER_BOTTOM", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCR_LOAD_ANIM_MOVE_LEFT", 5);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCR_LOAD_ANIM_MOVE_RIGHT", 6);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCR_LOAD_ANIM_MOVE_TOP", 7);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCR_LOAD_ANIM_MOVE_BOTTOM", 8);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCR_LOAD_ANIM_FADE_IN", 9);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCR_LOAD_ANIM_FADE_ON", 9);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCR_LOAD_ANIM_FADE_OUT", 10);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCR_LOAD_ANIM_OUT_LEFT", 11);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCR_LOAD_ANIM_OUT_RIGHT", 12);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCR_LOAD_ANIM_OUT_TOP", 13);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCR_LOAD_ANIM_OUT_BOTTOM", 14);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_TREE_WALK_NEXT", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_TREE_WALK_SKIP_CHILDREN", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_TREE_WALK_END", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_POINT_TRANSFORM_FLAG_NONE", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_POINT_TRANSFORM_FLAG_RECURSIVE", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_POINT_TRANSFORM_FLAG_INVERSE", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_POINT_TRANSFORM_FLAG_INVERSE_RECURSIVE", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCROLLBAR_MODE_OFF", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCROLLBAR_MODE_ON", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCROLLBAR_MODE_ACTIVE", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCROLLBAR_MODE_AUTO", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCROLL_SNAP_NONE", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCROLL_SNAP_START", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCROLL_SNAP_END", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCROLL_SNAP_CENTER", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_STATE_CMP_SAME", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_STATE_CMP_DIFF_REDRAW", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_STATE_CMP_DIFF_DRAW_PAD", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_STATE_CMP_DIFF_LAYOUT", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FS_RES_OK", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FS_RES_HW_ERR", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FS_RES_FS_ERR", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FS_RES_NOT_EX", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FS_RES_FULL", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FS_RES_LOCKED", 5);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FS_RES_DENIED", 6);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FS_RES_BUSY", 7);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FS_RES_TOUT", 8);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FS_RES_NOT_IMP", 9);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FS_RES_OUT_OF_MEM", 10);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FS_RES_INV_PARAM", 11);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FS_RES_UNKNOWN", 12);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FS_MODE_WR", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FS_MODE_RD", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FS_SEEK_SET", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FS_SEEK_CUR", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FS_SEEK_END", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_SRC_VARIABLE", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_SRC_FILE", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_SRC_SYMBOL", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_SRC_UNKNOWN", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DRAW_TASK_TYPE_NONE", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DRAW_TASK_TYPE_FILL", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DRAW_TASK_TYPE_BORDER", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DRAW_TASK_TYPE_BOX_SHADOW", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DRAW_TASK_TYPE_LETTER", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DRAW_TASK_TYPE_LABEL", 5);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DRAW_TASK_TYPE_IMAGE", 6);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DRAW_TASK_TYPE_LAYER", 7);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DRAW_TASK_TYPE_LINE", 8);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DRAW_TASK_TYPE_ARC", 9);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DRAW_TASK_TYPE_TRIANGLE", 10);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DRAW_TASK_TYPE_MASK_RECTANGLE", 11);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DRAW_TASK_TYPE_MASK_BITMAP", 12);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DRAW_TASK_TYPE_VECTOR", 13);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DRAW_TASK_STATE_WAITING", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DRAW_TASK_STATE_QUEUED", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DRAW_TASK_STATE_IN_PROGRESS", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DRAW_TASK_STATE_READY", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_LAYER_TYPE_NONE", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_LAYER_TYPE_SIMPLE", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_LAYER_TYPE_TRANSFORM", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_CLASS_EDITABLE_INHERIT", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_CLASS_EDITABLE_TRUE", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_CLASS_EDITABLE_FALSE", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_CLASS_GROUP_DEF_INHERIT", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_CLASS_GROUP_DEF_TRUE", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_CLASS_GROUP_DEF_FALSE", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_CLASS_THEME_INHERITABLE_FALSE", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_CLASS_THEME_INHERITABLE_TRUE", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_KEY_UP", 17);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_KEY_DOWN", 18);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_KEY_RIGHT", 19);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_KEY_LEFT", 20);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_KEY_ESC", 27);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_KEY_DEL", 127);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_KEY_BACKSPACE", 8);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_KEY_ENTER", 10);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_KEY_NEXT", 9);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_KEY_PREV", 11);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_KEY_HOME", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_KEY_END", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_GROUP_REFOCUS_POLICY_NEXT", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_GROUP_REFOCUS_POLICY_PREV", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_INDEV_TYPE_NONE", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_INDEV_TYPE_POINTER", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_INDEV_TYPE_KEYPAD", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_INDEV_TYPE_BUTTON", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_INDEV_TYPE_ENCODER", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_INDEV_STATE_RELEASED", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_INDEV_STATE_PRESSED", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_INDEV_MODE_NONE", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_INDEV_MODE_TIMER", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_INDEV_MODE_EVENT", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_INDEV_GESTURE_NONE", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_INDEV_GESTURE_PINCH", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_INDEV_GESTURE_SWIPE", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_INDEV_GESTURE_ROTATE", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_INDEV_GESTURE_TWO_FINGERS_SWIPE", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_INDEV_GESTURE_SCROLL", 5);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_INDEV_GESTURE_CNT", 6);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COVER_RES_COVER", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COVER_RES_NOT_COVER", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_COVER_RES_MASKED", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STATE_DEFAULT", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STATE_CHECKED", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STATE_FOCUSED", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STATE_FOCUS_KEY", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STATE_EDITED", 8);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STATE_HOVERED", 16);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STATE_PRESSED", 32);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STATE_SCROLLED", 64);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STATE_DISABLED", 128);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STATE_USER_1", 4096);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STATE_USER_2", 8192);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STATE_USER_3", 16384);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STATE_USER_4", 32768);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STATE_ANY", 65535);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PART_MAIN", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PART_SCROLLBAR", 65536);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PART_INDICATOR", 131072);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PART_KNOB", 196608);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PART_SELECTED", 262144);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PART_ITEMS", 327680);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PART_CURSOR", 393216);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PART_CUSTOM_FIRST", 524288);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PART_ANY", 983040);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_HIDDEN", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_CLICKABLE", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_CLICK_FOCUSABLE", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_CHECKABLE", 8);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_SCROLLABLE", 16);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_SCROLL_ELASTIC", 32);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_SCROLL_MOMENTUM", 64);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_SCROLL_ONE", 128);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_SCROLL_CHAIN_HOR", 256);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_SCROLL_CHAIN_VER", 512);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_SCROLL_CHAIN", 768);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_SCROLL_ON_FOCUS", 1024);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_SCROLL_WITH_ARROW", 2048);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_SNAPPABLE", 4096);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_PRESS_LOCK", 8192);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_EVENT_BUBBLE", 16384);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_GESTURE_BUBBLE", 32768);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_ADV_HITTEST", 65536);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_IGNORE_LAYOUT", 131072);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_FLOATING", 262144);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS", 524288);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_OVERFLOW_VISIBLE", 1048576);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_FLEX_IN_NEW_TRACK", 2097152);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_LAYOUT_1", 8388608);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_LAYOUT_2", 16777216);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_WIDGET_1", 33554432);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_WIDGET_2", 67108864);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_USER_1", 134217728);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_USER_2", 268435456);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_USER_3", 536870912);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_OBJ_FLAG_USER_4", 1073741824);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_FMT_TXT_CMAP_SPARSE_FULL", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_FMT_TXT_PLAIN", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_FMT_TXT_COMPRESSED", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_FMT_TXT_COMPRESSED_NO_PREFILTER", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FONT_FMT_PLAIN_ALIGNED", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_ALIGN_DEFAULT", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_ALIGN_TOP_LEFT", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_ALIGN_TOP_MID", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_ALIGN_TOP_RIGHT", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_ALIGN_BOTTOM_LEFT", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_ALIGN_BOTTOM_MID", 5);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_ALIGN_BOTTOM_RIGHT", 6);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_ALIGN_LEFT_MID", 7);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_ALIGN_RIGHT_MID", 8);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_ALIGN_CENTER", 9);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_ALIGN_AUTO_TRANSFORM", 10);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_ALIGN_STRETCH", 11);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_ALIGN_TILE", 12);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ANIM_IMAGE_PART_MAIN", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ARC_MODE_NORMAL", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ARC_MODE_SYMMETRICAL", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ARC_MODE_REVERSE", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_LABEL_LONG_MODE_WRAP", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_LABEL_LONG_MODE_DOTS", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_LABEL_LONG_MODE_SCROLL", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_LABEL_LONG_MODE_SCROLL_CIRCULAR", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_LABEL_LONG_MODE_CLIP", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BAR_MODE_NORMAL", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BAR_MODE_SYMMETRICAL", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BAR_MODE_RANGE", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BAR_ORIENTATION_AUTO", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BAR_ORIENTATION_HORIZONTAL", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BAR_ORIENTATION_VERTICAL", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_NONE", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_WIDTH_1", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_WIDTH_2", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_WIDTH_3", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_WIDTH_4", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_WIDTH_5", 5);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_WIDTH_6", 6);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_WIDTH_7", 7);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_WIDTH_8", 8);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_WIDTH_9", 9);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_WIDTH_10", 10);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_WIDTH_11", 11);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_WIDTH_12", 12);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_WIDTH_13", 13);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_WIDTH_14", 14);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_WIDTH_15", 15);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_HIDDEN", 16);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_NO_REPEAT", 32);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_DISABLED", 64);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_CHECKABLE", 128);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_CHECKED", 256);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_CLICK_TRIG", 512);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_POPOVER", 1024);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_RECOLOR", 2048);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_RESERVED_1", 4096);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_RESERVED_2", 8192);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_CUSTOM_1", 16384);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BUTTONMATRIX_CTRL_CUSTOM_2", 32768);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_CHART_TYPE_NONE", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_CHART_TYPE_LINE", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_CHART_TYPE_BAR", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_CHART_TYPE_SCATTER", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_CHART_UPDATE_MODE_SHIFT", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_CHART_UPDATE_MODE_CIRCULAR", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_CHART_AXIS_PRIMARY_Y", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_CHART_AXIS_SECONDARY_Y", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_CHART_AXIS_PRIMARY_X", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_CHART_AXIS_SECONDARY_X", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_CHART_AXIS_LAST", 5);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGEBUTTON_STATE_RELEASED", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGEBUTTON_STATE_PRESSED", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGEBUTTON_STATE_DISABLED", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGEBUTTON_STATE_CHECKED_RELEASED", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGEBUTTON_STATE_CHECKED_PRESSED", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGEBUTTON_STATE_CHECKED_DISABLED", 5);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGEBUTTON_STATE_NUM", 6);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_KEYBOARD_MODE_TEXT_LOWER", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_KEYBOARD_MODE_TEXT_UPPER", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_KEYBOARD_MODE_SPECIAL", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_KEYBOARD_MODE_NUMBER", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_KEYBOARD_MODE_USER_1", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_KEYBOARD_MODE_USER_2", 5);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_KEYBOARD_MODE_USER_3", 6);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_KEYBOARD_MODE_USER_4", 7);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_MENU_HEADER_TOP_FIXED", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_MENU_HEADER_TOP_UNFIXED", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_MENU_HEADER_BOTTOM_FIXED", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_MENU_ROOT_BACK_BUTTON_DISABLED", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_MENU_ROOT_BACK_BUTTON_ENABLED", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ROLLER_MODE_NORMAL", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_ROLLER_MODE_INFINITE", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCALE_MODE_HORIZONTAL_TOP", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCALE_MODE_HORIZONTAL_BOTTOM", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCALE_MODE_VERTICAL_LEFT", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCALE_MODE_VERTICAL_RIGHT", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCALE_MODE_ROUND_INNER", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCALE_MODE_ROUND_OUTER", 5);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SCALE_MODE_LAST", 6);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SLIDER_MODE_NORMAL", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SLIDER_MODE_SYMMETRICAL", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SLIDER_MODE_RANGE", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SLIDER_ORIENTATION_AUTO", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SLIDER_ORIENTATION_HORIZONTAL", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SLIDER_ORIENTATION_VERTICAL", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SPAN_OVERFLOW_CLIP", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SPAN_OVERFLOW_ELLIPSIS", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SPAN_OVERFLOW_LAST", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SPAN_MODE_FIXED", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SPAN_MODE_EXPAND", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SPAN_MODE_BREAK", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SPAN_MODE_LAST", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_PART_TEXTAREA_PLACEHOLDER", 524288);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SWITCH_ORIENTATION_AUTO", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SWITCH_ORIENTATION_HORIZONTAL", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SWITCH_ORIENTATION_VERTICAL", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TABLE_CELL_CTRL_NONE", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TABLE_CELL_CTRL_MERGE_RIGHT", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TABLE_CELL_CTRL_TEXT_CROP", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TABLE_CELL_CTRL_CUSTOM_1", 16);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TABLE_CELL_CTRL_CUSTOM_2", 32);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TABLE_CELL_CTRL_CUSTOM_3", 64);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_TABLE_CELL_CTRL_CUSTOM_4", 128);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SUBJECT_TYPE_INVALID", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SUBJECT_TYPE_NONE", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SUBJECT_TYPE_INT", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SUBJECT_TYPE_POINTER", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SUBJECT_TYPE_COLOR", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SUBJECT_TYPE_GROUP", 5);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_SUBJECT_TYPE_STRING", 6);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_GRIDNAV_CTRL_NONE", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_GRIDNAV_CTRL_ROLLOVER", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_GRIDNAV_CTRL_SCROLL_FIRST", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_GRIDNAV_CTRL_HORIZONTAL_MOVE_ONLY", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_GRIDNAV_CTRL_VERTICAL_MOVE_ONLY", 8);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FREETYPE_FONT_STYLE_NORMAL", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FREETYPE_FONT_STYLE_ITALIC", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FREETYPE_FONT_STYLE_BOLD", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FREETYPE_FONT_RENDER_MODE_BITMAP", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FREETYPE_FONT_RENDER_MODE_OUTLINE", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FREETYPE_OUTLINE_END", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FREETYPE_OUTLINE_MOVE_TO", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FREETYPE_OUTLINE_LINE_TO", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FREETYPE_OUTLINE_CUBIC_TO", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FREETYPE_OUTLINE_CONIC_TO", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_FREETYPE_OUTLINE_BORDER_START", 5);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_FLAGS_PREMULTIPLIED", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_FLAGS_COMPRESSED", 8);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_FLAGS_ALLOCATED", 16);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_FLAGS_MODIFIABLE", 32);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_FLAGS_CUSTOM_DRAW", 64);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_FLAGS_USER1", 256);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_FLAGS_USER2", 512);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_FLAGS_USER3", 1024);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_FLAGS_USER4", 2048);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_FLAGS_USER5", 4096);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_FLAGS_USER6", 8192);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_FLAGS_USER7", 16384);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMAGE_FLAGS_USER8", 32768);
    lvgl_binding_set_enum(lvgl_enum_obj, "FT_FONT_STYLE_NORMAL", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "FT_FONT_STYLE_ITALIC", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "FT_FONT_STYLE_BOLD", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DISP_ROTATION_0", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DISP_ROTATION_90", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DISP_ROTATION_180", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DISP_ROTATION_270", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DISP_RENDER_MODE_PARTIAL", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DISP_RENDER_MODE_DIRECT", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_DISP_RENDER_MODE_FULL", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BTNMATRIX_CTRL_HIDDEN", 16);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BTNMATRIX_CTRL_NO_REPEAT", 32);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BTNMATRIX_CTRL_DISABLED", 64);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BTNMATRIX_CTRL_CHECKABLE", 128);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BTNMATRIX_CTRL_CHECKED", 256);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BTNMATRIX_CTRL_CLICK_TRIG", 512);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BTNMATRIX_CTRL_POPOVER", 1024);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BTNMATRIX_CTRL_CUSTOM_1", 16384);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_BTNMATRIX_CTRL_CUSTOM_2", 32768);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMGBTN_STATE_RELEASED", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMGBTN_STATE_PRESSED", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMGBTN_STATE_DISABLED", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMGBTN_STATE_CHECKED_RELEASED", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMGBTN_STATE_CHECKED_PRESSED", 4);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_IMGBTN_STATE_CHECKED_DISABLED", 5);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_RES_OK", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_RES_INV", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_INDEV_STATE_PR", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_INDEV_STATE_REL", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_ANIM_TIME", 103);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_IMG_OPA", 68);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_IMG_RECOLOR", 69);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_IMG_RECOLOR_OPA", 70);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_SHADOW_OFS_X", 64);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_SHADOW_OFS_Y", 65);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_STYLE_TRANSFORM_ANGLE", 112);
    lvgl_binding_set_enum(lvgl_enum_obj, "_LV_EVENT_LAST", 66);
    lvgl_binding_set_enum(lvgl_enum_obj, "_LV_STYLE_LAST_BUILT_IN_PROP", 137);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_LABEL_LONG_WRAP", 0);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_LABEL_LONG_DOT", 1);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_LABEL_LONG_SCROLL", 2);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_LABEL_LONG_SCROLL_CIRCULAR", 3);
    lvgl_binding_set_enum(lvgl_enum_obj, "LV_LABEL_LONG_CLIP", 4);
    jerry_value_t global = jerry_current_realm();
    jerry_value_t key = jerry_string_sz("lvgl_enum");
    jerry_object_set(global, key, lvgl_enum_obj);
    jerry_value_free(key);
    jerry_value_free(global);
    jerry_value_free(lvgl_enum_obj);
}

/********************************** 初始化 LVGL 绑定系统 **********************************/
/**
 * @brief 初始化回调系统，注册 LVGL 对象删除事件处理函数，并注册 LVGL 函数
 */
void lv_binding_init() {
    lv_obj_add_event_cb(lv_scr_act(), lv_obj_deleted_cb, LV_EVENT_DELETE, NULL);
    appsys_register_functions(lvgl_binding_funcs, sizeof(lvgl_binding_funcs) / sizeof(AppSysFuncEntry));
    lv_bindings_special_init();
    register_lvgl_enums();
}
