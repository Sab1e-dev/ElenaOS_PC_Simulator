/**
 * @file lvgl_test.js
 * @brief 测试LVGL绑定函数的JavaScript代码
 * @author YourName
 * @date 2023-06-15
 */

let btn_click_count = 0;
function run_lvgl() {
  let startTime = new Date().getTime(); // 获取开始时间戳
  let duration = 3000; // 3秒 = 3000毫秒

  while (true) {
    let delay = lv_timer_handler();
    lv_delay_ms(delay);

    // 检查是否已经超过3秒
    let currentTime = new Date().getTime();
    if (currentTime - startTime >= duration) {
      break; // 退出循环
    }
  }
}
// 主测试函数
function test_lvgl_functions() {
  try {
    // 获取当前屏幕
    let scr = lv_scr_act();
    print("Screen object:", scr);

    // 测试lv_disp_get_scr_act
    let active_scr = lv_disp_get_scr_act();
    print("Active screen:", active_scr);

    // 创建基础对象
    let base_obj = lv_obj_create(scr);
    lv_obj_set_size(base_obj, 100, 50);
    lv_obj_align(base_obj, lvgl_enum.LV_ALIGN_TOP_MID, 0, 20);
    print("Base object created and positioned");

    // 测试标签功能
    test_label_functions(scr);

    // 测试按钮功能
    test_button_functions(scr);

    // 测试图像功能
    //test_image_functions(scr);

    // 测试滑块功能
    test_slider_functions(scr);

    // 测试开关功能
    test_switch_functions(scr);

    // 测试下拉菜单功能
    test_dropdown_functions(scr);

    // 测试文本区域功能
    test_textarea_functions(scr);

    // 测试复选框功能
    test_checkbox_functions(scr);

    // 测试弧形控件功能
    test_arc_functions(scr);

    // 测试进度条功能
    test_bar_functions(scr);

    // 测试图表功能
    test_chart_functions(scr);

    // 测试表格功能
    test_table_functions(scr);

    // 测试滚轮功能
    test_roller_functions(scr);

    // 测试消息框功能
    test_msgbox_functions(scr);

    // 测试样式功能
    test_style_functions(scr);

    // 测试事件功能
    test_event_functions(scr);

    // 测试对象管理功能
    test_object_management(scr);

    print("All LVGL function tests completed successfully!");
  } catch (e) {
    print("Test failed:", e.message);
  }
}

// 测试标签相关函数
function test_label_functions(scr) {
  print("\n=== Testing Label Functions ===");

  // 创建标签
  let label = lv_label_create(scr);
  lv_obj_align(label, lvgl_enum.LV_ALIGN_TOP_LEFT, 10, 10);

  // 设置文本
  lv_label_set_text(label, "Hello LVGL!");
  print("Label text set:", lv_label_get_text(label));

  // 测试长文本模式
  lv_label_set_long_mode(label, lvgl_enum.LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label, 100);
  lv_label_set_text(label, "This is a long text that should wrap automatically");

  // 测试重着色
  lv_label_set_recolor(label, true);
  lv_label_set_text(label, "#ff0000 Red# #00ff00 Green# #0000ff Blue#");

  print("Label functions tested successfully");
}

// 测试按钮相关函数
function test_button_functions(scr) {
  print("\n=== Testing Button Functions ===");

  // 创建按钮
  let btn = lv_btn_create(scr);
  lv_obj_set_size(btn, 100, 50);
  lv_obj_align(btn, lvgl_enum.LV_ALIGN_TOP_RIGHT, -10, 10);

  // 添加按钮标签
  let btn_label = lv_label_create(btn);
  lv_label_set_text(btn_label, "Click Me");
  lv_obj_center(btn_label);

  // 测试可检查按钮
  lv_obj_add_flag(btn, lvgl_enum.LV_OBJ_FLAG_CHECKABLE);

  //// 测试按钮状态
  lv_obj_set_state(btn, lvgl_enum.LV_STATE_PRESSED, lvgl_enum.LV_STATE_CHECKED);

  //// 添加点击事件
  register_lv_event_handler(
    btn,
    lvgl_enum.LV_EVENT_CLICKED,
    function (e) {
      let str = "Button click count:" + btn_click_count;
      btn_click_count += 1;
      print(str);
      lv_label_set_text(btn_label, "str");
      lv_btn_toggle(btn);
      
    }
  );
  print("Button functions tested successfully");
}

// 测试图像相关函数
function test_image_functions(scr) {
  print("\n=== Testing Image Functions ===");

  // 创建图像
  let img = lv_img_create(scr);
  lv_obj_align(img, lvgl_enum.LV_ALIGN_CENTER, -50, -50);

  // 设置图像源
  lv_img_set_src(img, "C:\\A.jpg");

  // 测试图像变换
  lv_img_set_zoom(img, 256); // 正常大小
  lv_img_set_angle(img, 0);  // 0度

  print("Image functions tested successfully");
}

// 测试滑块相关函数
function test_slider_functions(scr) {
  print("\n=== Testing Slider Functions ===");

  // 创建滑块
  let slider = lv_slider_create(scr);
  lv_obj_set_size(slider, 200, 20);
  lv_obj_align(slider, lvgl_enum.LV_ALIGN_CENTER, 0, 50);

  // 设置滑块范围
  lv_slider_set_range(slider, 0, 100);
  lv_slider_set_value(slider, 50, false);

  // 添加值改变事件
  register_lv_event_handler(
    slider,
    lvgl_enum.LV_EVENT_VALUE_CHANGED,
    function (e) {
      let val = lv_slider_get_value(slider);
      print("Slider value changed:", val);
    }
  );

  print("Slider functions tested successfully");
}

// 测试开关相关函数
function test_switch_functions(scr) {
  print("\n=== Testing Switch Functions ===");

  // 创建开关
  let sw = lv_switch_create(scr);
  lv_obj_align(sw, lvgl_enum.LV_ALIGN_CENTER, 0, 100);

  // 测试开关状态
  //lv_switch_on(sw, false);
  //print("Switch turned on");

  //// 添加值改变事件
  //register_lv_event_handler(
  //  sw,
  //  lvgl_enum.LV_EVENT_VALUE_CHANGED,
  //  function (e) {
  //    print("Switch state changed");
  //  }
  //);
  run_lvgl();
  print("Switch functions tested successfully");
}

// 测试下拉菜单相关函数
function test_dropdown_functions(scr) {
  print("\n=== Testing Dropdown Functions ===");

  // 创建下拉菜单
  let dropdown = lv_dropdown_create(scr);
  lv_obj_align(dropdown, lvgl_enum.LV_ALIGN_CENTER, 0, 150);

  // 设置选项
  lv_dropdown_set_options(dropdown, "Option 1\nOption 2\nOption 3");

  // 设置选中项
  lv_dropdown_set_selected(dropdown, 1);
  print("Dropdown selected:", lv_dropdown_get_selected(dropdown));

  print("Dropdown functions tested successfully");
}

// 测试文本区域相关函数
function test_textarea_functions(scr) {
  print("\n=== Testing Textarea Functions ===");

  // 创建文本区域
  let ta = lv_textarea_create(scr);
  lv_obj_set_size(ta, 200, 100);
  lv_obj_align(ta, lvgl_enum.LV_ALIGN_CENTER, 0, 200);

  // 设置文本
  lv_textarea_set_text(ta, "Initial text");
  lv_textarea_add_text(ta, "\nAdded text");

  // 设置占位文本
  lv_textarea_set_placeholder_text(ta, "Enter text here...");

  print("Textarea functions tested successfully");
}

// 测试复选框相关函数
function test_checkbox_functions(scr) {
  print("\n=== Testing Checkbox Functions ===");

  // 创建复选框
  let cb = lv_checkbox_create(scr);
  lv_obj_align(cb, lvgl_enum.LV_ALIGN_CENTER, -100, -150);

  // 设置文本
  lv_checkbox_set_text(cb, "Check me");

  // 测试选中状态
  print("Checkbox checked:", lv_checkbox_is_checked(cb));

  print("Checkbox functions tested successfully");
}

// 测试弧形控件相关函数
function test_arc_functions(scr) {
  print("\n=== Testing Arc Functions ===");

  // 创建弧形控件
  let arc = lv_arc_create(scr);
  lv_obj_set_size(arc, 150, 150);
  lv_obj_align(arc, lvgl_enum.LV_ALIGN_CENTER, 100, -150);

  // 设置范围
  lv_arc_set_range(arc, 0, 100);
  lv_arc_set_value(arc, 50);

  // 设置背景角度
  lv_arc_set_bg_angles(arc, 0, 270);

  print("Arc functions tested successfully");
}

// 测试进度条相关函数
function test_bar_functions(scr) {
  print("\n=== Testing Bar Functions ===");

  // 创建进度条
  let bar = lv_bar_create(scr);
  lv_obj_set_size(bar, 200, 20);
  lv_obj_align(bar, lvgl_enum.LV_ALIGN_CENTER, 0, -150);

  // 设置范围
  lv_bar_set_range(bar, 0, 100);
  lv_bar_set_value(bar, 75, false);

  print("Bar functions tested successfully");
}

// 测试图表相关函数
function test_chart_functions(scr) {
  print("\n=== Testing Chart Functions ===");

  // 创建图表
  let chart = lv_chart_create(scr);
  lv_obj_set_size(chart, 200, 150);
  lv_obj_align(chart, lvgl_enum.LV_ALIGN_CENTER, -150, 150);

  // 设置图表类型
  lv_chart_set_type(chart, lvgl_enum.LV_CHART_TYPE_LINE);

  // 设置点数
  lv_chart_set_point_count(chart, 10);

  // 设置范围
  lv_chart_set_range(chart, lvgl_enum.LV_CHART_AXIS_PRIMARY_Y, 0, 100);

  print("Chart functions tested successfully");
}

// 测试表格相关函数
function test_table_functions(scr) {
  print("\n=== Testing Table Functions ===");

  // 创建表格
  let table = lv_table_create(scr);
  lv_obj_set_size(table, 200, 150);
  lv_obj_align(table, lvgl_enum.LV_ALIGN_CENTER, 150, 150);

  // 设置行列数
  lv_table_set_col_cnt(table, 3);
  lv_table_set_row_cnt(table, 4);

  // 设置单元格值
  lv_table_set_cell_value(table, 0, 0, "Name");
  lv_table_set_cell_value(table, 0, 1, "Age");
  lv_table_set_cell_value(table, 0, 2, "Gender");

  print("Table functions tested successfully");
}

// 测试滚轮相关函数
function test_roller_functions(scr) {
  print("\n=== Testing Roller Functions ===");

  // 创建滚轮
  let roller = lv_roller_create(scr);
  lv_obj_align(roller, lvgl_enum.LV_ALIGN_CENTER, 150, -50);

  // 设置选项
  lv_roller_set_options(roller, "Option 1\nOption 2\nOption 3\nOption 4", lvgl_enum.LV_ROLLER_MODE_NORMAL);

  // 设置选中项
  lv_roller_set_selected(roller, 2, false);

  print("Roller functions tested successfully");
}

// 测试消息框相关函数
function test_msgbox_functions(scr) {
  print("\n=== Testing Msgbox Functions ===");

  // 创建消息框
  let mbox = lv_msgbox_create(scr, "Title", "Message text", ["OK", "Cancel"], true);
  lv_obj_align(mbox, lvgl_enum.LV_ALIGN_CENTER, 0, 0);

  // 添加按钮
  lv_msgbox_add_btns(mbox, ["Apply", "Close"]);

  // 关闭消息框
  lv_msgbox_close(mbox);

  print("Msgbox functions tested successfully");
}

// 测试样式相关函数
function test_style_functions(scr) {
  print("\n=== Testing Style Functions ===");

  // 创建测试对象
  let obj = lv_obj_create(scr);
  lv_obj_set_size(obj, 100, 100);
  lv_obj_align(obj, lvgl_enum.LV_ALIGN_CENTER, -150, -50);

  // 设置背景颜色
  lv_obj_set_style_bg_color(obj, { red: 255, green: 0, blue: 0 }, 0);

  // 设置文本颜色
  lv_obj_set_style_text_color(obj, { red: 255, green: 255, blue: 255 }, 0);

  // 设置圆角
  lv_obj_set_style_radius(obj, 10, 0);

  // 设置边框
  lv_obj_set_style_border_width(obj, 2, 0);
  lv_obj_set_style_border_color(obj, { red: 0, green: 0, blue: 255 }, 0);

  // 设置内边距
  lv_obj_set_style_pad_all(obj, 5, 0);
  lv_obj_set_style_pad_row(obj, 5, 0);
  lv_obj_set_style_pad_column(obj, 5, 0);

  // 设置字体
  // lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, 0);

  print("Style functions tested successfully");
}

// 测试事件相关函数
function test_event_functions(scr) {
  print("\n=== Testing Event Functions ===");

  // 创建测试按钮
  let btn = lv_btn_create(scr);
  lv_obj_set_size(btn, 80, 40);
  lv_obj_align(btn, lvgl_enum.LV_ALIGN_CENTER, 150, -100);

  let btn_label = lv_label_create(btn);
  lv_label_set_text(btn_label, "Test");
  lv_obj_center(btn_label);

  // 添加事件
  register_lv_event_handler(
    btn,
    lvgl_enum.LV_EVENT_CLICKED,
    function (e) {
      let code = lv_event_get_code(e);
      let target = lv_event_get_target(e);
      let user_data = lv_event_get_user_data(e);

      print("Event code:", code);
      print("Event target:", target);
      print("Event user data:", user_data);
    }
  );

  print("Event functions tested successfully");
}

// 测试对象管理相关函数
function test_object_management(scr) {
  print("\n=== Testing Object Management Functions ===");

  // 创建测试对象
  let obj = lv_obj_create(scr);
  lv_obj_set_size(obj, 50, 50);
  lv_obj_set_pos(obj, 10, 10);

  // 添加标志
  lv_obj_add_flag(obj, lvgl_enum.LV_OBJ_FLAG_CLICKABLE);

  // 清除标志
  lv_obj_clear_flag(obj, lvgl_enum.LV_OBJ_FLAG_CLICKABLE);

  // 添加样式
  // lv_obj_add_style(obj, &style, 0);

  // 删除对象
  lv_obj_del(obj);

  // 清理对象
  lv_obj_clean(scr);

  print("Object management functions tested successfully");
}

// 启动测试
test_lvgl_functions();
