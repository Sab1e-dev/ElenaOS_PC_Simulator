
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


/********************************** 函数系统 **********************************/

// 解析 lv_color_t 参数
lv_color_t js_to_lv_color(jerry_value_t js_color) {
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

jerry_value_t lv_color_to_js(lv_color_t color) {
    jerry_value_t js_color = jerry_object();

    // 添加RGB分量
    jerry_object_set(js_color, jerry_string_sz("r"), jerry_number(color.red));
    jerry_object_set(js_color, jerry_string_sz("g"), jerry_number(color.green));
    jerry_object_set(js_color, jerry_string_sz("b"), jerry_number(color.blue));

    // 添加十六进制颜色值
    uint32_t hex = (color.red << 16) | (color.green << 8) | color.blue;
    jerry_object_set(js_color, jerry_string_sz("hex"), jerry_number(hex));

    // 标记为LVGL颜色对象
    jerry_object_set(js_color, jerry_string_sz("__type"), jerry_string_sz("lv_color"));

    return js_color;
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
/********************************** 字体系统 **********************************/
static void register_lvgl_fonts(void) {
    jerry_value_t global = jerry_current_realm();

    // 创建字体对象容器
    jerry_value_t fonts = jerry_object();

    // 注册Montserrat字体集
#define REGISTER_FONT(name) do { \
        jerry_value_t font_obj = jerry_object(); \
        jerry_object_set(font_obj, jerry_string_sz("__ptr"), jerry_number((uintptr_t)&name)); \
        jerry_object_set(font_obj, jerry_string_sz("__type"), jerry_string_sz("lv_font")); \
        jerry_object_set(fonts, jerry_string_sz(#name), font_obj); \
        jerry_value_free(font_obj); \
    } while(0);

    REGISTER_FONT(lv_font_montserrat_14);
    REGISTER_FONT(lv_font_montserrat_20);
    /************************* 此处可以增加更多字体 ************************/

#undef REGISTER_FONT

    // 将字体容器挂载到全局对象
    jerry_object_set(global, jerry_string_sz("lv_font"), fonts);
    jerry_value_free(fonts);
    jerry_value_free(global);
}

const AppSysFuncEntry lvgl_binding_special_funcs[] = {
    { "lv_disp_get_scr_act", js_lv_disp_get_scr_act },
    { "lv_img_set_src", js_lv_img_set_src }
};

void lv_bindings_special_init(void) {
    // 初始化函数
    appsys_register_functions(lvgl_binding_special_funcs, sizeof(lvgl_binding_special_funcs) / sizeof(AppSysFuncEntry));
    register_lvgl_fonts();
}

