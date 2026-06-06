#include "camera.h"
#include "demos/lv_demos.h"
#include "esp_dma_utils.h"
#include "esp_heap_caps.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gesture_task.hpp"
#include "lvgl.h"
#include "ppa_conv.h"
#include "rpi_display.h"

static const char *TAG = "main";

static void lvgl_demo(lv_display_t *disp) {
    lv_obj_t *scr = lv_display_get_screen_active(disp);

    // Фон
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), LV_PART_MAIN);

    // Надпись
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Hello ESP32-P4!");
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -40);

    // Кнопка
    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 200, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 40);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Click me!");
    lv_obj_center(btn_label);
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting");

    // 1. Инициализация дисплея
    rpi_display_config_t cfg = RPI_DISPLAY_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(rpi_display_init(&cfg));

    // После rpi_display_init:
    ESP_LOGI(TAG, "Init camera...");
    void *cam_fb =
        heap_caps_aligned_alloc(128, CAMERA_FB_SIZE, MALLOC_CAP_SPIRAM);
    assert(cam_fb != NULL);
    ESP_ERROR_CHECK(camera_init(rpi_display_get_i2c_bus(), cam_fb));
    ESP_LOGI(TAG, "Camera OK, getting frames...");
    ESP_ERROR_CHECK(ppa_conv_init());

    size_t gesture_buf_size = 224 * 224 * 3;
    uint8_t *gesture_buf = NULL;
    esp_dma_mem_info_t dma_info = {
        .extra_heap_caps = MALLOC_CAP_SPIRAM,
        .dma_alignment_bytes = 64,
    };
    ESP_ERROR_CHECK(esp_dma_capable_malloc(gesture_buf_size, &dma_info,
                                           (void **)&gesture_buf, NULL));
    assert(gesture_buf != NULL);
    ESP_LOGI(TAG, "gesture_buf=%p align=%d", gesture_buf,
             (int)((uintptr_t)gesture_buf % 64));

    assert(gesture_buf != NULL);
    assert(((uintptr_t)gesture_buf % 64) == 0);
    gesture_task_init();

    // 2. LVGL port init
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    // 3. Добавляем дисплей в LVGL
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = NULL,
        .panel_handle = rpi_display_get_panel(),
        .buffer_size = RPI_DISPLAY_WIDTH * 150,
        .double_buffer = true,
        .hres = RPI_DISPLAY_WIDTH,
        .vres = RPI_DISPLAY_HEIGHT,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB888,
        .flags =
            {
                .buff_spiram = true,
                .sw_rotate = false,
                .direct_mode = 0,
            },
    };
    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags.avoid_tearing = false,
    };
    lv_display_t *disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);

    // Touch init
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = {
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 8,
        .flags.disable_control_phase = 1,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(rpi_display_get_i2c_bus(),
                                             &tp_io_cfg, &tp_io));

    esp_lcd_touch_handle_t tp = NULL;
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = RPI_DISPLAY_WIDTH,
        .y_max = RPI_DISPLAY_HEIGHT,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
        .flags =
            {
                .mirror_x = true,
                .mirror_y = true,
            },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(tp_io, &tp_cfg, &tp));

    // Подключаем к LVGL
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = tp,
    };
    lvgl_port_add_touch(&touch_cfg);

    // 4. Рисуем UI
    // Внутри lvgl_port_lock:
    // if (lvgl_port_lock(0)) {
    //    lv_demo_widgets();
    //    lvgl_port_unlock();
    //}

    ESP_LOGI(TAG, "Done");
    while (1) {
        esp_err_t ret = camera_get_frame();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Frame error: %s", esp_err_to_name(ret));
            continue;
        }

        // Дисплей: 800×640 → 800×480 RGB888
        ppa_conv_rgb565_to_rgb888(cam_fb, 800, 640,
                                  rpi_display_get_framebuffer(), 800, 480);

        // Жест: 800×640 → 128×128 RGB888 (аппаратный ресайз через PPA SRM)
        ppa_conv_rgb565_to_rgb888(cam_fb, 800, 640, gesture_buf, 224, 224);
        int label = gesture_task_run(gesture_buf, 224, 224);
        if (label != GESTURE_NONE) {
            ESP_LOGI(TAG, "Gesture: %d", label);
        }
    }
}
