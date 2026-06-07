#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Инициализация PPA клиента для конвертации RGB565 -> RGB888
 */
esp_err_t ppa_conv_init(void);

/**
 * @brief Конвертация RGB565 -> RGB888 через аппаратный PPA
 *
 * @param src      Входной буфер RGB565 (800x640)
 * @param src_w    Ширина входного изображения
 * @param src_h    Высота входного изображения
 * @param dst      Выходной буфер RGB888 (800x480 — crop по высоте)
 * @param dst_w    Ширина выходного изображения
 * @param dst_h    Высота выходного изображения
 */
esp_err_t ppa_conv_rgb565_to_rgb888(const void *src, uint32_t src_w,
                                    uint32_t src_h, void *dst, uint32_t dst_w,
                                    uint32_t dst_h);

esp_err_t ppa_conv_rgb888_to_rgb888(const void *src, uint32_t src_w,
                                    uint32_t src_h, void *dst, uint32_t dst_w,
                                    uint32_t dst_h);

esp_err_t ppa_cut_center_to224_rgb565_to_888(const void *src, uint32_t src_w,
                                    uint32_t src_h, void *dst, uint32_t dst_w,
                                    uint32_t dst_h);
/**
 * @brief Деинициализация PPA
 */
esp_err_t ppa_conv_deinit(void);

#ifdef __cplusplus
}
#endif
