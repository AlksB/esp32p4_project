idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES rpi_display esp_lcd esp_hw_support log freertos espressif__esp_lvgl_port lvgl__lvgl
)
