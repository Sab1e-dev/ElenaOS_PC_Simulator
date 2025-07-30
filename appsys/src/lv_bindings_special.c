
/**
 * @file lv_bindings_special.c
 * @brief 模块功能说明
 * @author Sab1e
 * @date
 */

#include "lv_bindings_special.h"
#include "jerryscript.h"
#include "lvgl\lvgl.h"
#include "appsys_core.h"
#include "appsys_port.h"

/********************************** 错误处理辅助函数 **********************************/
static jerry_value_t throw_error(const char* message) {
    jerry_value_t error_obj = jerry_error_sz(JERRY_ERROR_TYPE, (const jerry_char_t*)message);
    return jerry_throw_value(error_obj, true);
}

/********************************** 特殊 LVGL 函数 **********************************/

/**
 * @brief Return a pointer to the active screen on a display pointer to the active screen object (loaded by ' :ref:`lv_screen_load()` ')
 */
static jerry_value_t js_lv_disp_get_scr_act(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {

    void* disp = NULL;

    if (argc >= 1 && !jerry_value_is_null(args[0]) && !jerry_value_is_undefined(args[0])) {
        jerry_value_t js_disp = args[0];

        if (!jerry_value_is_object(js_disp)) {
            return throw_error("Argument 0 must be an object or null");
        }

        jerry_value_t disp_ptr_prop = jerry_string_sz("__ptr");
        jerry_value_t disp_ptr_val = jerry_object_get(js_disp, disp_ptr_prop);
        jerry_value_free(disp_ptr_prop);

        if (!jerry_value_is_number(disp_ptr_val)) {
            jerry_value_free(disp_ptr_val);
            return throw_error("Invalid __ptr property");
        }

        uintptr_t disp_ptr = (uintptr_t)jerry_value_as_number(disp_ptr_val);
        jerry_value_free(disp_ptr_val);
        disp = (void*)disp_ptr;
    }

    // 调用底层函数（disp 可能为 NULL）
    lv_obj_t* ret_value;
    ret_value = lv_display_get_screen_active(disp);
    
    if (!ret_value) {
        return jerry_null(); // 或者抛出错误：return throw_error("No active screen found");
    }

    // 包装为 JS 对象
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
 * @brief Set image source with string path support
 */
static jerry_value_t js_lv_img_set_src(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {
    // 参数检查
    if (argc < 2) {
        return throw_error("需要2个参数：图像对象和路径");
    }

    // 解析图像对象
    jerry_value_t js_img = args[0];
    if (!jerry_value_is_object(js_img)) {
        return throw_error("第一个参数必须是图像对象");
    }

    // 获取LVGL对象指针
    jerry_value_t ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t ptr_val = jerry_object_get(js_img, ptr_prop);
    jerry_value_free(ptr_prop);

    if (!jerry_value_is_number(ptr_val)) {
        jerry_value_free(ptr_val);
        return throw_error("无效的图像对象指针");
    }

    lv_obj_t* img = (lv_obj_t*)(uintptr_t)jerry_value_as_number(ptr_val);
    jerry_value_free(ptr_val);

    // 检查路径参数
    jerry_value_t js_path = args[1];
    if (!jerry_value_is_string(js_path)) {
        return throw_error("第二个参数必须是字符串路径");
    }

    // 转换路径字符串
    jerry_size_t len = jerry_string_length(js_path);
    char* path = (char*)malloc(len + 1);
    jerry_string_to_buffer(js_path, JERRY_ENCODING_UTF8, (jerry_char_t*)path, len);
    path[len] = '\0';

    // Windows路径处理：统一使用反斜杠
    for (char* p = path; *p; p++) {
        if (*p == '/') *p = '\\';
    }

    // 调用LVGL函数（关键修改点）
    lv_img_set_src(img, path);
    free(path);

    return jerry_undefined();
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
const AppSysFuncEntry lvgl_binding_special_funcs[] = {
    { "lv_disp_get_scr_act", js_lv_disp_get_scr_act },
    { "lv_img_set_src", js_lv_img_set_src },
    { "lv_scr_act", js_lv_scr_act }
};

void lv_bindings_special_init(void) {
    // 初始化函数
    appsys_register_functions(lvgl_binding_special_funcs, sizeof(lvgl_binding_special_funcs) / sizeof(AppSysFuncEntry));
}
