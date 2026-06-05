#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "rpi_display.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "main";

static void lvgl_demo(lv_display_t *disp)
{
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

void app_main(void)
{
    ESP_LOGI(TAG, "Starting");

    // 1. Инициализация дисплея
    rpi_display_config_t cfg = RPI_DISPLAY_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(rpi_display_init(&cfg));

    // 2. LVGL port init
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    // 3. Добавляем дисплей в LVGL
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle    = NULL,
        .panel_handle = rpi_display_get_panel(),
        .buffer_size  = RPI_DISPLAY_WIDTH * 30,
        .double_buffer = false,
        .hres         = RPI_DISPLAY_WIDTH,
        .vres         = RPI_DISPLAY_HEIGHT,
        .monochrome   = false,
        .color_format = LV_COLOR_FORMAT_RGB888,
        .flags = {
            .buff_spiram = true,
            .sw_rotate   = false,
        },
    };
    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags.avoid_tearing = false,
    };
    lv_display_t *disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);

    // 4. Рисуем UI
    if (lvgl_port_lock(0)) {
        lvgl_demo(disp);
        lvgl_port_unlock();
    }

    ESP_LOGI(TAG, "Done");
}
