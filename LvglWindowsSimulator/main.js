let scr = lv_scr_act();
let btn = lv_btn_create(scr);

register_lv_event_handler(
  btn,
  0x0000A,
  function (e) {
    print("Button clicked!");
    print("Event type: " + e.type);
  }
)
register_lv_event_handler(
  btn,
  0x00001,
  function (e) {
    print("Button pressed!");
    print("Event type: " + e.type);
  }
)

unregister_lv_event_handler(
  btn,
  0x00001
)


while (true) {
  lv_timer_handler();
  //delay(1);
}
