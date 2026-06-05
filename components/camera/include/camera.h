#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Разрешение камеры — 800x640 RAW8 50fps (ближайший поддерживаемый формат
// OV5647)
#define CAMERA_H_RES 800
#define CAMERA_V_RES 640
#define CAMERA_LANE_BITRATE 200 // Mbps

// Формат для отображения на дисплее (ISP конвертирует RAW8 -> RGB565)
#define CAMERA_OUTPUT_BPP 2 // RGB565 = 2 байта на пиксель
#define CAMERA_FB_SIZE (CAMERA_H_RES * CAMERA_V_RES * CAMERA_OUTPUT_BPP)

/**
 * @brief Инициализация камеры OV5647
 *
 * Использует уже существующий I2C bus (от rpi_display).
 * CSI → ISP → framebuffer (RGB565)
 *
 * @param i2c_bus  Существующий I2C bus handle
 * @param fb       Указатель на framebuffer куда ISP будет писать RGB565
 * @return ESP_OK или код ошибки
 */
esp_err_t camera_init(i2c_master_bus_handle_t i2c_bus, void *fb);

/**
 * @brief Получить один кадр (блокирующий вызов)
 */
esp_err_t camera_get_frame(void);

/**
 * @brief Деинициализация камеры
 */
esp_err_t camera_deinit(void);

#ifdef __cplusplus
}
#endif
