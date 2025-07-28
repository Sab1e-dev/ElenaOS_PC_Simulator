
/**
 * @file lvgl_binding.c
 * @brief 将 LVGL 绑定到 JerryScript 的实现文件
 * @author Sab1e
 * @date
 */

#include "lvgl_binding.h"
#include "jerryscript.h"
#include "uthash.h"
#include "lvgl/lvgl.h"
#include "appsys_core.h"
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
jerry_value_t register_lv_event_handler(const jerry_call_info_t* call_info_p,
    const jerry_value_t args[],
    const jerry_length_t arg_cnt) {
    if (arg_cnt < 3 || !jerry_value_is_object(args[0]) ||
        !jerry_value_is_number(args[1]) || !jerry_value_is_function(args[2])) {
        return jerry_exception_value(JERRY_ERROR_TYPE, (const jerry_char_t*)"Invalid arguments");
    }

    jerry_value_t ptr_val = jerry_object_get(args[0], jerry_string_sz("__ptr"));
    if (!jerry_value_is_number(ptr_val)) {
        jerry_value_free(ptr_val);
        return jerry_exception_value(JERRY_ERROR_TYPE, (const jerry_char_t*)"Invalid __ptr");
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
        return jerry_exception_value(JERRY_ERROR_RANGE, (const jerry_char_t*)"Too many callbacks");
    }

    return jerry_undefined();
}
/**
 * @brief 取消注册 LVGL 事件处理函数
 * @param args[0] lv_obj_t 对象
 * @param args[1] LVGL 事件类型（整数）
 * @return 无返回或抛出异常
 */
jerry_value_t unregister_lv_event_handler(const jerry_call_info_t* call_info_p,
    const jerry_value_t args[],
    const jerry_length_t arg_cnt) {
    if (arg_cnt < 2 || !jerry_value_is_object(args[0]) || !jerry_value_is_number(args[1])) {
        return jerry_exception_value(JERRY_ERROR_TYPE, (const jerry_char_t*)"Invalid arguments");
    }

    jerry_value_t ptr_val = jerry_object_get(args[0], jerry_string_sz("__ptr"));
    if (!jerry_value_is_number(ptr_val)) {
        jerry_value_free(ptr_val);
        return jerry_exception_value(JERRY_ERROR_TYPE, (const jerry_char_t*)"Invalid __ptr");
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

/********************************** 注册 LVGL 函数及回调 **********************************/


/**
 * @brief 函数列表
 */
const AppSysFuncEntry lvgl_binding_funcs [] = {
    {
        .name = "register_lv_event_handler",
        .handler = register_lv_event_handler
    },
    {
        .name = "unregister_lv_event_handler",
        .handler = unregister_lv_event_handler
}
};

/**
 * @brief 将函数注册到 JerryScript 全局对象中
 */
void lvgl_binding_register_functions() {
    jerry_value_t global = jerry_current_realm();
    for (size_t i = 0; i < sizeof(lvgl_binding_funcs) / sizeof(lvgl_binding_funcs[0]); ++i) {
        jerry_value_t fn = jerry_function_external(lvgl_binding_funcs[i].handler);
        jerry_value_t name = jerry_string_sz(lvgl_binding_funcs[i].name);
        jerry_object_set(global, name, fn);
        jerry_value_free(name);
        jerry_value_free(fn);
    }
    jerry_value_free(global);
}

/**
 * @brief 初始化回调系统，注册 LVGL 对象删除事件处理函数，并注册 LVGL 函数
 */
void lv_binding_init() {
    lv_obj_add_event_cb(lv_scr_act(), lv_obj_deleted_cb, LV_EVENT_DELETE, NULL);
    lvgl_binding_register_functions();
}
