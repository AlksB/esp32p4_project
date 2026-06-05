idf_component_register(
    SRCS "rpi_display.c"
    INCLUDE_DIRS
        "include"
        "$ENV{IDF_PATH}/components/esp_lcd/dsi"
        "$ENV{IDF_PATH}/components/hal/include"
        "$ENV{IDF_PATH}/components/hal/esp32p4/include"
    REQUIRES
        esp_driver_i2c
        esp_lcd
        esp_hw_support
        esp_mm
        log
        freertos
        hal
)
