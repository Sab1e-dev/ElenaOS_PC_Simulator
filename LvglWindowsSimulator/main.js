let scr = lv_scr_act();
let click_count = 0;
try { 
  // 创建按钮
  let btn = lv_btn_create(scr);
  lv_obj_set_size(btn, 120, 50);
  lv_obj_align(btn, lvgl_enum.LV_ALIGN_CENTER, 0, -40);
  lv_obj_set_style_bg_color(btn, { red: 0, green: 128, blue: 255 }, 0);

  // 按钮上的标签
  let btn_label = lv_label_create(btn);
  lv_label_set_text(btn_label, "Click me");
  lv_obj_center(btn_label);

  // 注册按钮点击事件
  register_lv_event_handler(
    btn,
    lvgl_enum.LV_EVENT_CLICKED,
    function (e) {
      print("Button clicked!");
      click_count++;
      let label_text = lv_label_get_text(btn_label);
      lv_label_set_text(btn_label, label_text + click_count);
    }
  );

  // 创建滑块
  let slider = lv_slider_create(scr);
  lv_obj_set_size(slider, 200, 20);
  lv_obj_align(slider, lvgl_enum.LV_ALIGN_CENTER, 0, 40);
  lv_slider_set_range(slider, 0, 100);
  lv_slider_set_value(slider, 50, false);

  // 滑块值显示
  let slider_label = lv_label_create(scr);
  lv_label_set_text(slider_label, "Slider: 50");
  lv_obj_align_to(slider_label, slider, lvgl_enum.LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

  // 滑块事件处理
  register_lv_event_handler(
    slider,
    lvgl_enum.LV_EVENT_VALUE_CHANGED,
    function (e) {
      let val = lv_slider_get_value(slider);
      lv_label_set_text(slider_label, "Value: " + val.toString());
    }
  );
} catch (e) {
  throw e.message;
}


// 主循环
while (true) {
  lv_timer_handler();
  // delay(1); // 实际运行时请使用平台相关延迟函数
}
