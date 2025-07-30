import json
import os
from datetime import date
import sys

# 配置路径
script_dir = os.path.dirname(os.path.abspath(__file__))
input_json_path = '../LvglPlatform/lvgl/scripts/gen_json/output/lvgl.json'
output_c_path = 'output/lv_bindings.c'
export_functions_path = os.path.join(script_dir, "export_functions.txt")
blacklist_functions_path = os.path.join(script_dir, "blacklist_functions.txt")

# 目标代码的固定部分
HEADER_CODE = r"""
/**
 * @file lv_bindings.c
 * @brief 将 LVGL 绑定到 JerryScript 的实现文件，此文件使用脚本自动生成。
 * @author Sab1e
 * @date """ + date.today().strftime("%Y-%m-%d") + r"""
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
"""

INIT_FUNCTION_CODE = r"""
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
"""

def parse_type(type_info):
    """解析类型信息，返回C类型字符串和类型信息字典"""
    if not type_info:
        return ('void', {'is_void': True})
    
    if type_info.get('json_type') == 'ret_type':
        return parse_type(type_info.get('type'))
    
    if type_info.get('json_type') == 'pointer':
        base_type, base_info = parse_type(type_info.get('type'))
        quals = ' '.join(type_info.get('quals', []))
        if quals:
            return (f"{quals} {base_type}*", {'is_pointer': True, 'base_type': base_info})
        return (f"{base_type}*", {'is_pointer': True, 'base_type': base_info})
    
    if 'name' in type_info:
        quals = ' '.join(type_info.get('quals', []))
        type_name = type_info['name']
        type_info_dict = {'type_name': type_name}
        
        if type_info.get('json_type') == 'primitive_type':
            type_info_dict['is_primitive'] = True
        elif type_info.get('json_type') == 'stdlib_type':
            type_info_dict['is_stdlib'] = True
        
        if quals:
            return (f"{quals} {type_name}", type_info_dict)
        return (type_name, type_info_dict)
    
    return ('void', {'is_void': True})

def is_void_type(type_str):
    """检查是否是void类型"""
    return 'void' in type_str and '*' not in type_str

def is_lv_color_t(type_str):
    """检查是否是 lv_color_t 类型"""
    return 'lv_color_t' in type_str

def is_void_pointer(type_str):
    """检查是否是void指针类型"""
    return 'void*' in type_str.replace(' ', '')

def is_lv_obj_pointer(type_str):
    """检查是否是lv_obj_t指针类型"""
    return type_str.replace(' ', '') == 'lv_obj_t*'

def is_object_pointer(type_str):
    """检查是否是对象指针类型"""
    return type_str.endswith('*') and ('lv_' in type_str or 'obj' in type_str)

def is_string_pointer(type_str):
    """检查是否是字符串指针类型"""
    normalized = type_str.replace(' ', '')
    return normalized in ['char*', 'constchar*', 'const char*', 'char const*']

def is_number_type(type_str):
    """检查是否是数字类型"""
    number_types = ['int', 'float', 'double', 'uint8_t', 'uint16_t', 'uint32_t', 
                   'int8_t', 'int16_t', 'int32_t', 'size_t', 'bool', 'short', 
                   'long', 'unsigned', 'signed', 'uintptr_t']
    return any(num_type in type_str for num_type in number_types)

def is_lvgl_value_type(type_str):
    """检查是否是LVGL值类型（非指针）"""
    return 'lv_' in type_str and '*' not in type_str

def is_enum_type(type_name, typedefs_data):
    """检查是否是枚举类型"""
    for enum in typedefs_data.get('enums', []):
        if enum['name'] == type_name:
            return True
    return False

def is_typedef_convertible_to_basic(typedef_name, typedefs_data):
    """检查typedef是否可以转换为基本类型"""
    if is_enum_type(typedef_name, typedefs_data):
        return True
        
    for typedef in typedefs_data.get('typedefs', []):
        if typedef['name'] == typedef_name:
            base_type, type_info = parse_type(typedef['type'])
            
            if type_info.get('is_pointer') and type_info['base_type'].get('is_void'):
                return True
            
            if type_info.get('is_primitive') or type_info.get('is_stdlib'):
                return True
                    
    return False

def get_typedef_base_type(typedef_name, typedefs_data):
    """获取typedef对应的基础类型"""
    if is_enum_type(typedef_name, typedefs_data):
        return 'int'
        
    for typedef in typedefs_data.get('typedefs', []):
        if typedef['name'] == typedef_name:
            base_type, type_info = parse_type(typedef['type'])
            
            if type_info.get('is_pointer') and type_info['base_type'].get('is_void'):
                return 'uintptr_t'
            
            if type_info.get('is_primitive') or type_info.get('is_stdlib'):
                return base_type
            
    return None

def generate_void_pointer_arg_parsing(index, name):
    """生成void指针类型参数解析代码"""
    return fr"""    // void* 类型参数
    void* {name} = NULL;
    if (!jerry_value_is_undefined(args[{index}])) {{
        if (jerry_value_is_object(args[{index}])) {{
            // 尝试从对象获取指针
            jerry_value_t ptr_prop = jerry_string_sz("__ptr");
            jerry_value_t ptr_val = jerry_object_get(args[{index}], ptr_prop);
            jerry_value_free(ptr_prop);
            
            if (jerry_value_is_number(ptr_val)) {{
                uintptr_t ptr_num = (uintptr_t)jerry_value_as_number(ptr_val);
                {name} = (void*)ptr_num;
            }}
            jerry_value_free(ptr_val);
        }}
        else if (jerry_value_is_number(args[{index}])) {{
            // 直接传递指针数值
            uintptr_t ptr_num = (uintptr_t)jerry_value_as_number(args[{index}]);
            {name} = (void*)ptr_num;
        }}
        else {{
            return throw_error("Argument {index} must be object or number for void*");
        }}
    }}
    
"""

def generate_generic_pointer_arg_parsing(index, name, type_str):
    """生成通用指针类型参数解析代码"""
    return fr"""    // 通用指针类型: {type_str}
    void* {name} = NULL;
    if (!jerry_value_is_undefined(args[{index}])) {{
        if (jerry_value_is_object(args[{index}])) {{
            // 尝试从对象获取指针
            jerry_value_t ptr_prop = jerry_string_sz("__ptr");
            jerry_value_t ptr_val = jerry_object_get(args[{index}], ptr_prop);
            jerry_value_free(ptr_prop);
            
            if (jerry_value_is_number(ptr_val)) {{
                uintptr_t ptr_num = (uintptr_t)jerry_value_as_number(ptr_val);
                {name} = (void*)ptr_num;
            }}
            jerry_value_free(ptr_val);
        }}
        else if (jerry_value_is_number(args[{index}])) {{
            // 直接传递指针数值
            uintptr_t ptr_num = (uintptr_t)jerry_value_as_number(args[{index}]);
            {name} = (void*)ptr_num;
        }}
        else {{
            return throw_error("Argument {index} must be object or number for {type_str}");
        }}
    }}
    
"""

def generate_object_arg_parsing(index, name):
    """生成对象类型参数解析代码"""
    return fr"""    jerry_value_t js_{name} = args[{index}];
    if (!jerry_value_is_object(js_{name})) {{
        return throw_error("Argument {index} must be an object");
    }}
    
    jerry_value_t {name}_ptr_prop = jerry_string_sz("__ptr");
    jerry_value_t {name}_ptr_val = jerry_object_get(js_{name}, {name}_ptr_prop);
    jerry_value_free({name}_ptr_prop);
    
    if (!jerry_value_is_number({name}_ptr_val)) {{
        jerry_value_free({name}_ptr_val);
        return throw_error("Invalid __ptr property");
    }}
    
    uintptr_t {name}_ptr = (uintptr_t)jerry_value_as_number({name}_ptr_val);
    jerry_value_free({name}_ptr_val);
    void* {name} = (void*){name}_ptr;
    
"""

def generate_string_arg_parsing(index, name):
    """生成字符串类型参数解析代码，使用动态内存分配"""
    return fr"""    jerry_value_t js_{name} = args[{index}];
    if (!jerry_value_is_string(js_{name})) {{
        return throw_error("Argument {index} must be a string");
    }}
    
    jerry_size_t {name}_len = jerry_string_size(js_{name}, JERRY_ENCODING_UTF8);
    char* {name} = (char*)malloc({name}_len + 1);
    if (!{name}) {{
        return throw_error("Out of memory");
    }}
    jerry_string_to_buffer(js_{name}, JERRY_ENCODING_UTF8, (jerry_char_t*){name}, {name}_len);
    {name}[{name}_len] = '\0';

"""

def generate_number_arg_parsing(index, name, type_str):
    """生成数字类型参数解析代码"""
    return fr"""    jerry_value_t js_{name} = args[{index}];
    if (!jerry_value_is_number(js_{name})) {{
        return throw_error("Argument {index} must be a number");
    }}
    
    {type_str} {name} = ({type_str})jerry_value_as_number(js_{name});
    
"""

def generate_bool_arg_parsing(index, name):
    """生成布尔类型参数解析代码"""
    return fr"""    jerry_value_t js_{name} = args[{index}];
    if (!jerry_value_is_boolean(js_{name})) {{
        return throw_error("Argument {index} must be a boolean");
    }}
    
    bool {name} = jerry_value_to_boolean(js_{name});
    
"""

def generate_arg_parsing(index, name, arg_type, type_info, typedefs_data):
    """根据类型信息生成参数解析代码"""
    type_str = arg_type.replace(' ', '')
    
    # 特殊处理lv_obj_t指针
    if is_lv_obj_pointer(type_str):
        return generate_object_arg_parsing(index, name)
    
    # 处理基本类型
    if is_void_type(type_str):
        return f"    // void 类型跳过解析\n"
    elif is_void_pointer(type_str):
        return generate_void_pointer_arg_parsing(index, name)
    elif is_object_pointer(type_str):
        return generate_object_arg_parsing(index, name)
    elif is_string_pointer(type_str):
        return generate_string_arg_parsing(index, name)
    elif is_lv_color_t(type_str):
        return f"    lv_color_t {name} = parse_lv_color(args[{index}]);\n\n"
    if type_str == 'bool' or (is_typedef_convertible_to_basic(type_info.get('type_name', ''), typedefs_data) 
                             and get_typedef_base_type(type_info['type_name'], typedefs_data) == 'bool'):
        return fr"""    // 布尔类型或兼容类型参数: {name}
    bool {name} = false;
    if (!jerry_value_is_undefined(args[{index}])) {{
        if (jerry_value_is_boolean(args[{index}])) {{
            {name} = jerry_value_to_boolean(args[{index}]);
        }}
        else if (jerry_value_is_number(args[{index}])) {{
            {name} = (jerry_value_as_number(args[{index}]) != 0);
        }}
        else {{
            return throw_error("Argument {index} must be boolean or number for {type_str}");
        }}
    }}
    
"""
    elif is_number_type(type_str):
        return generate_number_arg_parsing(index, name, type_str)
    
    # 检查是否是typedef类型
    type_name = type_info.get('type_name', '')
    if type_name:
        base_type = get_typedef_base_type(type_name, typedefs_data)
        if base_type:
            if base_type == 'bool':
                return generate_bool_arg_parsing(index, name)
            elif is_number_type(base_type):
                return generate_number_arg_parsing(index, name, base_type)
    
    # 默认处理为通用指针
    return generate_generic_pointer_arg_parsing(index, name, type_str)

def generate_void_pointer_return():
    """生成void指针返回值的处理代码"""
    return """    // 包装为通用指针对象
    jerry_value_t js_obj = jerry_object();
    jerry_value_t ptr = jerry_number((double)(uintptr_t)ret_value);
    
    jerry_object_set(js_obj, jerry_string_sz("__ptr"), ptr);
    jerry_object_set(js_obj, jerry_string_sz("__type"), jerry_string_sz("void*"));
    
    jerry_value_free(ptr);
    return js_obj;
"""

def generate_generic_pointer_return():
    """生成通用指针返回值的处理代码"""
    return """    // 包装为通用指针对象
    jerry_value_t js_obj = jerry_object();
    jerry_value_t ptr = jerry_number((double)(uintptr_t)ret_value);
    
    jerry_object_set(js_obj, jerry_string_sz("__ptr"), ptr);
    
    jerry_value_free(ptr);
    return js_obj;
"""

def generate_object_return():
    """生成对象返回值的处理代码"""
    return """    // 包装为LVGL对象
    jerry_value_t js_obj = jerry_object();
    jerry_value_t ptr = jerry_number((double)(uintptr_t)ret_value);
    jerry_value_t cls = jerry_string_sz("lv_obj");
    
    jerry_object_set(js_obj, jerry_string_sz("__ptr"), ptr);
    jerry_object_set(js_obj, jerry_string_sz("__class"), cls);
    
    jerry_value_free(ptr);
    jerry_value_free(cls);
    return js_obj;
"""

def generate_string_return():
    """生成字符串返回值的处理代码"""
    return """    if (ret_value == NULL) {
        return jerry_string_sz("");
    }
    return jerry_string_sz((const jerry_char_t*)ret_value);
"""

def generate_number_return():
    """生成数字返回值的处理代码"""
    return """    return jerry_number(ret_value);
"""

def generate_bool_return():
    """生成布尔返回值的处理代码"""
    return """    return jerry_boolean(ret_value);
"""

def find_real_function_definition(func_name, data):
    """
    查找函数的真实定义，处理宏定义的情况
    返回 (真实函数名, 函数定义)
    """
    # 首先检查是否是宏定义
    for macro in data.get('macros', []):
        if macro['name'] == func_name and 'initializer' in macro:
            # 提取宏定义的底层函数名
            real_func_name = macro['initializer'].strip()
            if real_func_name.endswith(';'):
                real_func_name = real_func_name[:-1].strip()
            
            # 查找底层函数定义
            for func in data.get('functions', []):
                if func['name'] == real_func_name:
                    return (real_func_name, func)
            
            # 如果没有找到函数定义，尝试查找函数指针
            for func_ptr in data.get('function_pointers', []):
                if func_ptr['name'] == real_func_name:
                    return (real_func_name, func_ptr)
    
    # 如果不是宏定义，直接查找函数定义
    for func in data.get('functions', []):
        if func['name'] == func_name:
            return (func_name, func)
    
    # 查找函数指针定义
    for func_ptr in data.get('function_pointers', []):
        if func_ptr['name'] == func_name:
            return (func_name, func_ptr)
    
    return (None, None)

def generate_binding_function(func, typedefs_data):
    """生成绑定函数实现"""
    func_name = func['name']
    
    # 查找真实函数定义
    real_func_name, real_func_def = find_real_function_definition(func_name, typedefs_data)
    if not real_func_def:
        print(f"[警告] 未找到函数 {func_name} 的定义")
        return ""
    
    # 使用真实函数定义生成代码
    return_type = 'void'
    return_type_info = {'is_void': True}
    
    if 'type' in real_func_def:
        return_type, return_type_info = parse_type(real_func_def['type'])
    
    args = real_func_def.get('args', [])
    if len(args) == 1 and is_void_type(parse_type(args[0].get('type', {}))[0]):
        args = []
    
    docstring = real_func_def.get('docstring', '').strip() or f"{func_name} function (aliased to {real_func_name})"

    code = f"""
/**
 * {docstring}
 */
static jerry_value_t js_{func_name}(const jerry_call_info_t* info,
    const jerry_value_t args[],
    const jerry_length_t argc) {{
"""

    if args:
        code += f"    // 参数数量检查\n"
        code += f"    if (argc < {len(args)}) {{\n"
        code += f"        return throw_error(\"Insufficient arguments\");\n"
        code += f"    }}\n\n"

    string_arg_names = []

    for i, arg in enumerate(args):
        arg_name = arg.get('name', f"arg{i}")
        arg_type = 'void'
        arg_type_info = {'is_void': True}
        
        if 'type' in arg:
            arg_type, arg_type_info = parse_type(arg['type'])
            
        code += f"    // 解析参数: {arg_name} ({arg_type})\n"
        code += generate_arg_parsing(i, arg_name, arg_type, arg_type_info, typedefs_data)
        
        if is_string_pointer(arg_type):
            string_arg_names.append(arg_name)

    has_void_arg = len(args) == 1 and is_void_type(parse_type(args[0].get('type', {}))[0])
    args_list = "" if has_void_arg else ", ".join([f"{arg.get('name', f'arg{i}')}" for i, arg in enumerate(args)])

    return_stmt = "    // 调用底层函数\n"
    
    if return_type == 'void':
        return_stmt += f"    {real_func_name}({args_list});\n"
    else:
        return_stmt += f"    {return_type} ret_value = {real_func_name}({args_list});\n\n"
        return_stmt += "    // 处理返回值\n"

        if is_void_pointer(return_type):
            return_stmt += generate_void_pointer_return()
        elif is_lv_obj_pointer(return_type):
            return_stmt += generate_object_return()
        elif is_string_pointer(return_type):
            return_stmt += generate_string_return()
        elif is_number_type(return_type) or is_lvgl_value_type(return_type):
            return_stmt += generate_number_return()
        elif return_type == 'bool':
            return_stmt += generate_bool_return()
        else:
            # 处理typedef返回值
            type_name = return_type_info.get('type_name', '')
            if type_name:
                base_type = get_typedef_base_type(type_name, typedefs_data)
                if base_type:
                    if base_type == 'bool':
                        return_stmt += generate_bool_return()
                    elif is_number_type(base_type):
                        return_stmt += generate_number_return()
                    else:
                        return_stmt += generate_generic_pointer_return()
                else:
                    return_stmt += generate_generic_pointer_return()
            else:
                return_stmt += generate_generic_pointer_return()

    # 释放动态分配的字符串内存
    for name in string_arg_names:
        return_stmt += f"    free({name});\n"

    if return_type == 'void':
        return_stmt += f"    return jerry_undefined();\n"

    code += return_stmt
    code += "}\n"
    return code

def generate_native_funcs_list(functions):
    """生成原生函数列表数组"""
    entries = []
    entries.append('    { "register_lv_event_handler", register_lv_event_handler }')
    entries.append('    { "unregister_lv_event_handler", unregister_lv_event_handler }')
    
    for func in functions:
        func_name = func['name']
        entries.append(f'    {{ "{func_name}", js_{func_name} }}')
    
    entries_str = ',\n'.join(entries)
    return f"""
const AppSysFuncEntry lvgl_binding_funcs[] = {{
{entries_str}
}};

const unsigned int lvgl_binding_funcs_count = {len(functions)+2};
"""

def generate_enum_binding(enums, macros=None):
    """生成枚举绑定代码，处理enums和macros"""
    lines = []
    lines.append("static void lvgl_binding_set_enum(jerry_value_t obj, const char* key, int32_t val) {")
    lines.append("    jerry_value_t jkey = jerry_string_sz(key);")
    lines.append("    jerry_value_t jval = jerry_number(val);")
    lines.append("    jerry_object_set(obj, jkey, jval);")
    lines.append("    jerry_value_free(jkey);")
    lines.append("    jerry_value_free(jval);")
    lines.append("}")
    lines.append("")
    lines.append("void register_lvgl_enums(void) {")
    lines.append("    jerry_value_t lvgl_enum_obj = jerry_object();")
    
    # 第一步：收集所有枚举成员名和值的映射
    enum_members = {}
    for enum in enums:
        if enum is None:  # 检查enum是否为None
            continue
        for member in enum.get('members', []):
            if member is None:  # 检查member是否为None
                continue
            name = member.get('name')
            value = member.get('value')
            if name and value is not None:
                try:
                    enum_members[name] = int(value, 0)  # 支持十六进制
                except ValueError:
                    continue
    
    # 第二步：处理所有宏定义
    if macros:
        macro_definitions = {}
        for macro in macros:
            if macro is None:  # 检查macro是否为None
                continue
            macro_name = macro.get('name')
            initializer = macro.get('initializer', '')
            if initializer is None:  # 检查initializer是否为None
                initializer = ''
            initializer = initializer.strip()
            
            # 清理初始值（去除分号和空格）
            if initializer.endswith(';'):
                initializer = initializer[:-1].strip()
            
            # 检查初始值是否是已知的枚举成员
            if initializer in enum_members:
                macro_definitions[macro_name] = enum_members[initializer]
        
        # 将宏定义添加到枚举成员中
        enum_members.update(macro_definitions)
    
    # 第三步：生成枚举绑定代码
    for name, value in enum_members.items():
        lines.append(f'    lvgl_binding_set_enum(lvgl_enum_obj, "{name}", {value});')
    
    lines.append("    jerry_value_t global = jerry_current_realm();")
    lines.append('    jerry_value_t key = jerry_string_sz("lvgl_enum");')
    lines.append("    jerry_object_set(global, key, lvgl_enum_obj);")
    lines.append("    jerry_value_free(key);")
    lines.append("    jerry_value_free(global);")
    lines.append("    jerry_value_free(lvgl_enum_obj);")
    lines.append("}")
    return "\n".join(lines)

def load_blacklist_functions(file_path):
    """加载黑名单函数列表"""
    blacklist = set()
    if os.path.exists(file_path):
        with open(file_path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#"):
                    blacklist.add(line)
        print(f"[已加载] 黑名单函数列表 {file_path} 中共有 {len(blacklist)} 个函数")
    else:
        print(f"[提示] 未找到黑名单函数列表文件: {file_path}")
    return blacklist

def load_export_functions(file_path):
    """加载导出函数列表"""
    functions = set()
    lines = []
    if os.path.exists(file_path):
        with open(file_path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#"):
                    if line not in functions:
                        lines.append(line)
                    functions.add(line)

        with open(file_path, "w", encoding="utf-8") as f:
            for func in lines:
                f.write(func + "\n")
        print(f"[已自动去重并更新] {file_path} 中共保留 {len(lines)} 个唯一函数名")
    else:
        print(f"[警告] 未找到函数导出列表文件: {file_path}")
    return functions


def main():
    # 从同级目录读取lvgl.json
    if not os.path.exists(input_json_path):
        print(f"❌ 找不到输入文件: {input_json_path}")
        return
    
    with open(input_json_path, 'r') as f:
        data = json.load(f)
    
    # 生成绑定函数
    binding_code = ""
    EXPORT_FUNCTIONS = load_export_functions(export_functions_path)
    BLACKLIST_FUNCTIONS = load_blacklist_functions(blacklist_functions_path)  # 加载黑名单
    
    # 收集所有需要导出的函数（排除黑名单中的函数）
    exported_funcs = []
    for func_name in EXPORT_FUNCTIONS:
        if func_name in BLACKLIST_FUNCTIONS:
            print(f"[跳过] 函数 {func_name} 在黑名单中，不生成绑定代码")
            continue
            
        # 查找函数定义，包括宏定义
        real_func_name, real_func_def = find_real_function_definition(func_name, data)
        if real_func_def:
            # 创建一个新的字典，保留原始函数名
            func_copy = real_func_def.copy()
            func_copy['name'] = func_name  # 保留原始函数名
            exported_funcs.append(func_copy)
        else:
            print(f"[警告] 未找到函数 {func_name} 的定义")
    
    if not exported_funcs:
        print("❌ 没有找到匹配的导出函数，请检查 export_functions.txt 文件。")
        return
    
    # 生成函数声明
    func_decls = ''.join([
        f"static jerry_value_t js_{func['name']}(const jerry_call_info_t*, const jerry_value_t*, jerry_length_t);\n"
        for func in exported_funcs
    ])
    
    # 生成函数实现
    for func in exported_funcs:
        binding_code += generate_binding_function(func, data)
        binding_code += "\n\n"
    
    # 生成函数列表
    func_list = generate_native_funcs_list(exported_funcs)
    
    # 从JSON数据中获取enums和macros
    enums = data.get('enums', [])
    macros = data.get('macros', [])
    
    # 生成枚举绑定
    enum_binding = generate_enum_binding(enums, macros)
    
    # 构建完整的C代码
    output = (
        HEADER_CODE +
        "// 函数声明\n" +
        func_decls + "\n" +
        "// 函数实现\n" +
        binding_code +
        func_list + "\n" +
        enum_binding + "\n" +
        INIT_FUNCTION_CODE
    )
    
    # 输出到.c文件
    os.makedirs(os.path.dirname(output_c_path), exist_ok=True)
    with open(output_c_path, "w", encoding="utf-8") as f:
        f.write(output)
    print(f"✅ 生成C代码已写入: {output_c_path}")

if __name__ == "__main__":
    for arg in sys.argv:
        if arg.startswith('--json-path='):
            input_json_path = arg.split('=', 1)[1]
        elif arg.startswith('--output-c-path='):
            output_c_path = arg.split('=', 1)[1]
    main()