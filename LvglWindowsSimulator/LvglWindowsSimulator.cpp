#include <Windows.h>

#include <LvglWindowsIconResource.h>

#include "lvgl/lvgl.h"
#include "lvgl/examples/lv_examples.h"
#include "lvgl/demos/lv_demos.h"

#include "jerryscript.h"
#include "appsys_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <direct.h>

#define LVGL_WINDOW_WIDTH 800
#define LVGL_WINDOW_HEIGHT 480

char* load_js_file(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("Failed to open JS file: %s\n", filename);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long len = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(len + 1);
    if (!buffer) {
        printf("Out of memory while reading JS\n");
        fclose(file);
        return NULL;
    }

    fread(buffer, 1, len, file);
    buffer[len] = '\0'; // Null-terminate
    fclose(file);

    return buffer;
}

int main()
{
    lv_init();

    /*
        * Optional workaround for users who wants UTF-8 console output.
        * If you don't want that behavior can comment them out.
        *
        * Suggested by jinsc123654.
        */
#if LV_TXT_ENC == LV_TXT_ENC_UTF8
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif

    int32_t zoom_level = 100;
    bool allow_dpi_override = false;
    bool simulator_mode = true;
    lv_display_t* display = lv_windows_create_display(
        L"ElenaOS Simulator",
        LVGL_WINDOW_WIDTH,
        LVGL_WINDOW_HEIGHT,
        zoom_level,
        allow_dpi_override,
        simulator_mode);
    if (!display)
    {
        return -1;
    }

    HWND window_handle = lv_windows_get_display_window_handle(display);
    if (!window_handle)
    {
        return -1;
    }

    HICON icon_handle = LoadIconW(
        GetModuleHandleW(NULL),
        MAKEINTRESOURCE(IDI_LVGL_WINDOWS));
    if (icon_handle)
    {
        SendMessageW(
            window_handle,
            WM_SETICON,
            TRUE,
            (LPARAM)icon_handle);
        SendMessageW(
            window_handle,
            WM_SETICON,
            FALSE,
            (LPARAM)icon_handle);
    }

    lv_indev_t* pointer_indev = lv_windows_acquire_pointer_indev(display);
    if (!pointer_indev)
    {
        return -1;
    }

    lv_indev_t* keypad_indev = lv_windows_acquire_keypad_indev(display);
    if (!keypad_indev)
    {
        return -1;
    }

    lv_indev_t* encoder_indev = lv_windows_acquire_encoder_indev(display);
    if (!encoder_indev)
    {
        return -1;
    }

    //lv_demo_widgets();
    //lv_demo_benchmark();

    /*LV_IMAGE_DECLARE(A);
    lv_obj_t* img = lv_img_create(lv_scr_act());
    lv_img_set_src(img, &A);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);*/
    /*LV_IMAGE_DECLARE(B);
    lv_obj_t* img = lv_img_create(lv_scr_act());
    lv_img_set_src(img, &B);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);*/
    char cwd[256];
    _getcwd(cwd, sizeof(cwd));
    printf("Current working directory: %s\n", cwd);

    char* script = load_js_file("main.js");
    if (!script) return 0;

    ApplicationPackage_t app = {
        .app_id = "com.mydev.clock",
        .name = "时钟",
        .version = "1.0.2",
        .author = "Sab1e",
        .description = "一个简单的时钟应用",
        .mainjs_str = (char*)script };



    while (1) {
        appsys_run_app(&app);
        printf("\nPress enter to continue...\n");
        char c;
        scanf("%c", &c);
        Sleep(1000); // 等待 1 秒后重新运行应用
    }
    //while (1)
    //{
    //    uint32_t time_till_next = lv_timer_handler();
    //    lv_delay_ms(time_till_next);
    //}

    return 0;
}

//#include <stdio.h>
//#include "jerryscript.h"
//#include "jerryscript-port.h"
//#include <stdlib.h>
//#include "appsys_core.h"
//
//int main(void) {
//    const jerry_char_t script[] = "print('Hello from JerryScript!', 123, true);";
//
//    ApplicationPackage_t app = {
//        .app_id = "com.mydev.clock",
//        .name = "时钟",
//        .version = "1.0.2",
//        .author = "Sab1e",
//        .description = "一个简单的时钟应用",
//        .mainjs_str = (const char*)script};
//       
//    appsys_run_app(&app);
//
//    const jerry_char_t newsc[] = "while(true){print('Hello from JerryScript!', 123, true,app_info.app_id+app_info.app_id);delay(100);}";
//
//    app = {
//        .app_id = "com.mydev.clock",
//        .name = "时钟",
//        .version = "1.0.2",
//        .author = "Sab1e",
//        .description = "一个简单的时钟应用",
//        .mainjs_str = (const char*)newsc };
//
//    appsys_run_app(&app);
//
//    return 0;
//}
