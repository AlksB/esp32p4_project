#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "rpi_display.h"
#include "mipi_dsi_priv.h"  // должен быть первым среди esp_lcd includes


void app_main(void)
{
    ESP_LOGI(TAG, "Starting RPi 7\" display demo");

    rpi_display_config_t cfg = RPI_DISPLAY_DEFAULT_CONFIG();

    esp_err_t ret = rpi_display_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Display init OK");

    // Заливаем framebuffer красным для проверки
    void *fb = rpi_display_get_framebuffer();
    if (fb) {
        uint8_t *p = (uint8_t *)fb;
        for (int i = 0; i < RPI_DISPLAY_WIDTH * RPI_DISPLAY_HEIGHT; i++) {
            p[i * 3 + 2] = 0xff; // R
            p[i * 3 + 1] = 0x00; // G
            p[i * 3 + 0] = 0x00; // B
        }
        rpi_display_flush_framebuffer();
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
