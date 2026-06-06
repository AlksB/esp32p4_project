idf_component_register(
    SRCS "main.c" "gesture_task.cpp"
    INCLUDE_DIRS "."
    REQUIRES rpi_display camera esp_lcd esp_hw_support log freertos espressif__esp_lvgl_port lvgl__lvgl
)
