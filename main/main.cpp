#include "camera.h"
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
#include "soc/gpio_num.h"
#include <cstring>

static const char *TAG = "main";

static const char *GESTURE_NAMES[] = {"one",  "two",     "three",  "four",
                                      "five", "like",    "ok",     "no_gesture",
                                      "call", "dislike", "no_hand"};

// Камера 800×640 вписывается в дисплей 800×480 по высоте
// Ширина: 480 * 800/640 = 600, отступ слева: (800-600)/2 = 100
static const int CAM_DISP_W = 600;
static const int CAM_DISP_H = 480;
static const int CAM_DISP_X = (RPI_DISPLAY_WIDTH - CAM_DISP_W) / 2;

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting");

    // Дисплей
    rpi_display_config_t cfg = RPI_DISPLAY_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(rpi_display_init(&cfg));

    // Камера
    ESP_LOGI(TAG, "Init camera...");
    void *cam_fb =
        heap_caps_aligned_alloc(128, CAMERA_FB_SIZE, MALLOC_CAP_SPIRAM);
    assert(cam_fb != NULL);
    ESP_ERROR_CHECK(camera_init(rpi_display_get_i2c_bus(), cam_fb));
    ESP_LOGI(TAG, "Camera OK");
    ESP_ERROR_CHECK(ppa_conv_init());

    // Буфер для инференса 224×224 RGB888
    size_t gesture_buf_size = 224 * 224 * 3;
    uint8_t *gesture_buf = nullptr;
    esp_dma_mem_info_t gesture_dma_info = {
        .extra_heap_caps = MALLOC_CAP_SPIRAM,
        .dma_alignment_bytes = 64,
    };
    ESP_ERROR_CHECK(esp_dma_capable_malloc(gesture_buf_size, &gesture_dma_info,
                                           (void **)&gesture_buf, nullptr));
    assert(gesture_buf != nullptr);

    gesture_task_init();

    // LVGL
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = nullptr,
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
    lvgl_port_display_dsi_cfg_t dsi_cfg = {};
    dsi_cfg.flags.avoid_tearing = false;
    lv_display_t *disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);

    // Touch
    esp_lcd_panel_io_handle_t tp_io = nullptr;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = {};
    tp_io_cfg.dev_addr = ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS;
    tp_io_cfg.control_phase_bytes = 1;
    tp_io_cfg.dc_bit_offset = 0;
    tp_io_cfg.lcd_cmd_bits = 8;
    tp_io_cfg.flags.disable_control_phase = 1;
    tp_io_cfg.scl_speed_hz = 100000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(rpi_display_get_i2c_bus(),
                                             &tp_io_cfg, &tp_io));
    esp_lcd_touch_handle_t tp = nullptr;
    esp_lcd_touch_config_t tp_cfg = {};
    tp_cfg.x_max = RPI_DISPLAY_WIDTH;
    tp_cfg.y_max = RPI_DISPLAY_HEIGHT;
    tp_cfg.rst_gpio_num = (gpio_num_t)-1;
    tp_cfg.int_gpio_num = (gpio_num_t)-1;
    tp_cfg.flags.mirror_x = true;
    tp_cfg.flags.mirror_y = true;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(tp_io, &tp_cfg, &tp));
    const lvgl_port_touch_cfg_t touch_cfg = {.disp = disp, .handle = tp};
    lvgl_port_add_touch(&touch_cfg);

    // Буфер canvas 600×480 RGB888
    size_t canvas_buf_size = CAM_DISP_W * CAM_DISP_H * 3;
    esp_dma_mem_info_t canvas_dma_info = {
        .extra_heap_caps = MALLOC_CAP_SPIRAM,
        .dma_alignment_bytes = 64,
    };
    void *canvas_buf = nullptr;
    ESP_ERROR_CHECK(esp_dma_capable_malloc(canvas_buf_size, &canvas_dma_info,
                                           &canvas_buf, nullptr));
    memset(canvas_buf, 0, canvas_buf_size);

    // UI
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), LV_PART_MAIN);

    // Canvas с изображением камеры по центру
    lv_obj_t *canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(canvas, canvas_buf, CAM_DISP_W, CAM_DISP_H,
                         LV_COLOR_FORMAT_RGB888);
    lv_obj_set_pos(canvas, CAM_DISP_X, 0);

    // Прямоугольник вокруг руки
    lv_obj_t *hand_rect = lv_obj_create(scr);
    lv_obj_remove_style_all(hand_rect);
    lv_obj_set_style_border_color(hand_rect, lv_color_hex(0x00ff00), 0);
    lv_obj_set_style_border_width(hand_rect, 3, 0);
    lv_obj_set_style_border_opa(hand_rect, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(hand_rect, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(hand_rect, 0, 0);
    lv_obj_add_flag(hand_rect, LV_OBJ_FLAG_HIDDEN);

    // Метка жеста
    lv_obj_t *gesture_label = lv_label_create(scr);
    lv_obj_set_style_text_color(gesture_label, lv_color_hex(0x00ff00), 0);
    lv_obj_set_style_text_font(gesture_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(gesture_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(gesture_label, LV_OPA_50, 0);
    lv_label_set_text(gesture_label, "");
    lv_obj_add_flag(gesture_label, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "Done");

    while (1) {
        esp_err_t ret = camera_get_frame();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Frame error: %s", esp_err_to_name(ret));
            continue;
        }

        // Инференс: 800×640 → 224×224
        ppa_conv_rgb565_to_rgb888(cam_fb, 800, 640, gesture_buf, 224, 224);
        gesture_result_t res = gesture_task_run(gesture_buf, 224, 224);

        // Камера → canvas: 800×640 → 600×480 (сохранение пропорций)
        ppa_conv_rgb565_to_rgb888(cam_fb, 800, 640, canvas_buf, CAM_DISP_W,
                                  CAM_DISP_H);

        // Пересчёт bbox: 800×480 (из gesture_task) → 600×480
        if (res.has_hand) {
            res.x1 = res.x1 * CAM_DISP_W / 800;
            res.y1 = res.y1 * CAM_DISP_H / 480;
            res.x2 = res.x2 * CAM_DISP_W / 800;
            res.y2 = res.y2 * CAM_DISP_H / 480;
        }

        if (lvgl_port_lock(0)) {
            lv_obj_invalidate(canvas);

            if (res.has_hand) {
                lv_obj_clear_flag(hand_rect, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(gesture_label, LV_OBJ_FLAG_HIDDEN);

                // Позиция на экране = смещение canvas + позиция внутри canvas
                lv_obj_set_pos(hand_rect, CAM_DISP_X + res.x1, res.y1);
                lv_obj_set_size(hand_rect, res.x2 - res.x1, res.y2 - res.y1);
                lv_obj_set_pos(gesture_label, CAM_DISP_X + res.x1,
                               res.y1 > 20 ? res.y1 - 20 : 0);
                if (res.label >= 0 && res.label < 11) {
                    lv_label_set_text(gesture_label, GESTURE_NAMES[res.label]);
                }
            } else {
                lv_obj_add_flag(hand_rect, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(gesture_label, LV_OBJ_FLAG_HIDDEN);
            }
            lvgl_port_unlock();
        }
    }
}
