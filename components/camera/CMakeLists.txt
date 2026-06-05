idf_component_register(
    SRCS "camera.c" "ppa_conv.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_i2c
        esp_driver_isp
        esp_driver_cam
        esp_driver_ppa
        esp_hw_support
        log
        freertos
        espressif__esp_cam_sensor
        espressif__esp_sccb_intf
)
