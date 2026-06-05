idf_component_register(
    SRCS "camera.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_i2c
        esp_driver_isp
        esp_driver_cam
        esp_hw_support
        log
        freertos
        espressif__esp_cam_sensor
        espressif__esp_sccb_intf
)
