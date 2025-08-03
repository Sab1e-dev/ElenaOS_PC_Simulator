/**
 * @file main.js
 * @brief 测试LVGL绑定函数的JavaScript代码
 * @author Sab1e
 * @date 2025-08-03
 */

let btn_click_count = 0;
function run_lvgl(loop) {
  let startTime = new Date().getTime(); // 获取开始时间戳
  let duration = 3000; // 3秒 = 3000毫秒

  while (true) {
    let delay = lv_timer_handler();
    lv_delay_ms(delay);

    // 检查是否已经超过3秒
    if (!loop) {
      let currentTime = new Date().getTime();
      if (currentTime - startTime >= duration) {
        break; // 退出循环
      }
    }
  }
}
// 主测试函数
function test_lvgl_functions() {
  try {
    // 获取当前屏幕
    let scr = lv_scr_act();
    print("Screen object:", scr);
    
    let label=lv_label_create(scr);
    lv_label_set_text(label, "Red label");
    let st = {};
    lv_style_init(st);
    let color = lv_color_hex(0xff0000);
    print(color.hex);
    lv_style_set_text_color(st, color);
    lv_obj_add_style(label, st, LV_PART_MAIN);
    run_lvgl(true);
  } catch (e) {
    print("Test failed:", e.message);
  }
}

// 启动测试
test_lvgl_functions();
