
/**
 * @file appsys_native_func.c
 * @brief 原生函数实现及注册
 * @author Sab1e
 * @date 2025-07-26
 */
#include "appsys_native_func.h"
#include "jerryscript.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "lvgl/lvgl.h"
/********************************** 原生函数定义 **********************************/
/**
 * @brief 处理 JavaScript 的 print 调用，将所有参数转换为字符串并打印到标准输出。每个参数之间以空格分隔，末尾换行。适用于 JerryScript 引擎的原生函数绑定。
 * @param call_info_p 指向调用信息的指针，当前未使用，可忽略。类型为 const jerry_call_info_t*。
 * @param args_p 参数数组，包含所有传入的 JavaScript 参数。类型为 const jerry_value_t[]。
 * @param args_count 参数数量，表示 args_p 数组的长度。类型为 jerry_length_t。
 * @return 返回一个未定义值（jerry_undefined），表示没有返回结果。
 */
jerry_value_t js_print_handler(const jerry_call_info_t* call_info_p,
    const jerry_value_t args_p[],
    const jerry_length_t args_count)
{
    (void)call_info_p; // 当前未用到 this/func_obj，可忽略

    for (jerry_length_t i = 0; i < args_count; i++) {
        jerry_value_t str_val;

        if (jerry_value_is_string(args_p[i])) {
            str_val = jerry_value_copy(args_p[i]);
        }
        else {
            str_val = jerry_value_to_string(args_p[i]);  // 转为字符串
        }

        jerry_size_t size = jerry_string_size(str_val, JERRY_ENCODING_UTF8);
        jerry_char_t* buf = (jerry_char_t*)malloc(size + 1); // Explicitly cast void* to jerry_char_t*
        if (!buf) {
            jerry_value_free(str_val);
            continue;
        }

        jerry_string_to_buffer(str_val, JERRY_ENCODING_UTF8, buf, size);
        buf[size] = '\0';

        printf("%s", buf);
        if (i < args_count - 1) {
            printf(" ");
        }

        free(buf);
        jerry_value_free(str_val);
    }

    printf("\n");
    return jerry_undefined();
}

jerry_value_t js_delay_handler(const jerry_call_info_t* call_info_p,
    const jerry_value_t args_p[],
    const jerry_length_t args_count)
{
    Sleep(args_p[0]);
}

static jerry_value_t js_lv_btn_create(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    if (argc < 1 || !jerry_value_is_object(args[0])) {
        return jerry_undefined();
    }

    // 从 JS 对象中提取原始指针
    jerry_value_t js_parent = args[0];
    jerry_value_t ptr_val = jerry_object_get(js_parent, jerry_string_sz((const jerry_char_t*)"__ptr"));

    if (!jerry_value_is_number(ptr_val)) {
        jerry_value_free(ptr_val);
        return jerry_undefined();
    }

    uintptr_t parent_ptr = (uintptr_t)jerry_value_as_number(ptr_val);
    lv_obj_t* parent = (lv_obj_t*)parent_ptr;

    jerry_value_free(ptr_val);

    // 调用 LVGL 原生函数
    lv_obj_t* btn = lv_btn_create(parent);

    // 包装为 JS 对象返回
    jerry_value_t js_obj = jerry_object();

    jerry_value_t ptr = jerry_number((double)(uintptr_t)btn);
    jerry_value_t cls = jerry_string_sz((const jerry_char_t*)"lv_obj");

    jerry_object_set(js_obj, jerry_string_sz((const jerry_char_t*)"__ptr"), ptr);
    jerry_object_set(js_obj, jerry_string_sz((const jerry_char_t*)"__class"), cls);

    jerry_value_free(ptr);
    jerry_value_free(cls);

    return js_obj;
}

static jerry_value_t js_lv_scr_act(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 调用 LVGL 的原始函数
    lv_obj_t* scr = lv_scr_act();

    // 包装为 JS 对象
    jerry_value_t js_obj = jerry_object();

    jerry_value_t ptr = jerry_number((double)(uintptr_t)scr);
    jerry_value_t cls = jerry_string_sz((const jerry_char_t*)"lv_obj");

    jerry_object_set(js_obj, jerry_string_sz((const jerry_char_t*)"__ptr"), ptr);
    jerry_object_set(js_obj, jerry_string_sz((const jerry_char_t*)"__class"), cls);

    jerry_value_free(ptr);
    jerry_value_free(cls);

    return js_obj;
}
static jerry_value_t js_lv_timer_handler(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    lv_timer_handler();
    return jerry_undefined(); // 无返回值
}
/********************************** 注册原生函数 **********************************/
/**
 * @brief 原生函数入口链接结构体
 */
typedef struct {
    const char* name;
    jerry_external_handler_t handler;
} NativeFuncEntry;

/**
 * @brief 原生函数列表
 */
const NativeFuncEntry appsys_native_funcs[] = {
    {
        .name = "print",
        .handler = js_print_handler
    },
    {
        .name = "delay",
        .handler = js_delay_handler
    },
    {
        .name = "lv_btn_create",
        .handler = js_lv_btn_create
    },
    {
        .name = "lv_scr_act",
        .handler = js_lv_scr_act
    },
    {
        .name = "lv_timer_handler",
        .handler = js_lv_timer_handler
    }
};

/**
 * @brief 将原生函数注册到 JerryScript 全局对象中
 */
void appsys_register_natives() {
    jerry_value_t global = jerry_current_realm();
    for (size_t i = 0; i < sizeof(appsys_native_funcs) / sizeof(appsys_native_funcs[0]); ++i) {
        jerry_value_t fn = jerry_function_external(appsys_native_funcs[i].handler);
        jerry_value_t name = jerry_string_sz(appsys_native_funcs[i].name);
        jerry_object_set(global, name, fn);
        jerry_value_free(name);
        jerry_value_free(fn);
    }
    jerry_value_free(global);
}
