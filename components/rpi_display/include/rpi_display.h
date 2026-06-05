#pragma once

#include "esp_err.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

// ─── Display geometry ────────────────────────────────────────────────────────
#define RPI_DISPLAY_WIDTH   800
#define RPI_DISPLAY_HEIGHT  480

// ─── I2C pins ────────────────────────────────────────────────────────────────
#define RPI_DISPLAY_SCL_GPIO  8
#define RPI_DISPLAY_SDA_GPIO  7

// ─── DSI config ──────────────────────────────────────────────────────────────
#define RPI_DISPLAY_DSI_LANE_NUM      1
#define RPI_DISPLAY_DSI_LANE_MBPS     600

// ─── LDO config ──────────────────────────────────────────────────────────────
#define RPI_DISPLAY_LDO_CHAN_ID       3
#define RPI_DISPLAY_LDO_VOLTAGE_MV    2500

/**
 * @brief Display configuration structure
 */
typedef struct {
    int     scl_gpio;        /*!< I2C SCL GPIO */
    int     sda_gpio;        /*!< I2C SDA GPIO */
    int     ldo_chan_id;     /*!< LDO channel for MIPI PHY */
    int     ldo_voltage_mv;  /*!< LDO voltage in mV */
    int     dsi_lane_num;    /*!< Number of DSI data lanes */
    int     dsi_lane_mbps;   /*!< DSI lane bit rate in Mbps */
} rpi_display_config_t;

/**
 * @brief Default display configuration
 */
#define RPI_DISPLAY_DEFAULT_CONFIG() {          \
    .scl_gpio       = RPI_DISPLAY_SCL_GPIO,     \
    .sda_gpio       = RPI_DISPLAY_SDA_GPIO,     \
    .ldo_chan_id    = RPI_DISPLAY_LDO_CHAN_ID,  \
    .ldo_voltage_mv = RPI_DISPLAY_LDO_VOLTAGE_MV, \
    .dsi_lane_num   = RPI_DISPLAY_DSI_LANE_NUM, \
    .dsi_lane_mbps  = RPI_DISPLAY_DSI_LANE_MBPS, \
}

/**
 * @brief Initialize the RPi 7" display
 *
 * Full initialization sequence:
 * 1. I2C + ATtiny88 power-on (v1.1 regulator protocol)
 * 2. LDO for MIPI PHY
 * 3. DSI bus
 * 4. DPI panel + NON-BURST mode
 * 5. Release TC358762 reset via ATtiny proxy
 * 6. TC358762 bridge init via DSI Generic Long Write
 * 7. Backlight on
 *
 * @param cfg Display configuration
 * @return ESP_OK on success
 */
esp_err_t rpi_display_init(const rpi_display_config_t *cfg);

/**
 * @brief Deinitialize the display and free resources
 */
esp_err_t rpi_display_deinit(void);

/**
 * @brief Get the DPI panel handle
 */
esp_lcd_panel_handle_t rpi_display_get_panel(void);

/**
 * @brief Get the DSI bus handle
 */
esp_lcd_dsi_bus_handle_t rpi_display_get_dsi_bus(void);

/**
 * @brief Get framebuffer pointer (RGB888, 800x480x3 bytes)
 */
void *rpi_display_get_framebuffer(void);

/**
 * @brief Flush framebuffer to display (cache sync)
 */
esp_err_t rpi_display_flush_framebuffer(void);

/**
 * @brief Set backlight brightness (0-255)
 */
esp_err_t rpi_display_set_brightness(uint8_t brightness);

#ifdef __cplusplus
}
#endif
