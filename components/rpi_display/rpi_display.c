#include "rpi_display.h"

#include "driver/i2c_master.h"
#include "esp_cache.h"
#include "esp_check.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_private/esp_cache_private.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/mipi_dsi_hal.h"
#include "hal/mipi_dsi_ll.h"
#include "hal/mipi_dsi_types.h"
#include "mipi_dsi_priv.h"
#include <string.h>

#undef TAG
static const char *TAG = "rpi_display";

// ─── ATtiny88 registers ───────────────────────────────────────────────────────
#define ATTINY_ADDR     0x45
#define REG_ID          0x80
#define REG_PORTA       0x81
#define REG_PORTB       0x82
#define REG_PORTC       0x83
#define REG_PWM         0x86

// ATtiny88 SPI proxy registers for writing TC358762
#define REG_ADDR_L       0x8c
#define REG_ADDR_H       0x8d
#define REG_WRITE_DATA_H 0x90
#define REG_WRITE_DATA_L 0x91

// REG_PORTA bits
#define PA_LCD_LR       BIT(2)  // Horizontal scan direction

// REG_PORTB bits
#define PB_LCD_MAIN     BIT(7)  // Main regulator enable

// REG_PORTC bits
#define PC_LED_EN       BIT(0)  // Backlight LED
#define PC_RST_TP_N     BIT(1)  // Touch reset (active low)
#define PC_RST_LCD_N    BIT(2)  // LCD reset (active low)
#define PC_RST_BRIDGE_N BIT(3)  // TC358762 reset (active low)

// ─── TC358762 registers ───────────────────────────────────────────────────────
#define TC_DSI_LANEENABLE       0x0210
#define TC_PPI_D0S_CLRSIPOCOUNT 0x0164
#define TC_PPI_D1S_CLRSIPOCOUNT 0x0168
#define TC_PPI_D0S_ATMR         0x0144
#define TC_PPI_D1S_ATMR         0x0148
#define TC_PPI_LPTXTIMECNT      0x0114
#define TC_PPI_STARTPPI         0x0104
#define TC_DSI_STARTDSI         0x0204
#define TC_SPICMR               0x0450
#define TC_LCDCTRL              0x0420
#define TC_SYSCTRL              0x0464
#define TC_LCD_HS_HBP           0x0424
#define TC_LCD_HDISP_HFP        0x0428
#define TC_LCD_VS_VBP           0x042c
#define TC_LCD_VDISP_VFP        0x0430
#define TC_IDREG                0x04A0

// ─── RPi 7" timing (from panel-raspberrypi-touchscreen.c) ────────────────────
#define RPI_MODE_HSW  2
#define RPI_MODE_HBP  46
#define RPI_MODE_HFP  210
#define RPI_MODE_VSW  20
#define RPI_MODE_VBP  4
#define RPI_MODE_VFP  22

// ─── State ───────────────────────────────────────────────────────────────────
static i2c_master_bus_handle_t  s_i2c_bus    = NULL;
static i2c_master_dev_handle_t  s_attiny     = NULL;
static esp_ldo_channel_handle_t s_ldo        = NULL;
static esp_lcd_dsi_bus_handle_t s_dsi_bus    = NULL;
static esp_lcd_panel_handle_t   s_panel      = NULL;
static void                    *s_fb         = NULL;

// ─── I2C helpers ─────────────────────────────────────────────────────────────
static esp_err_t attiny_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_attiny, buf, 2, 100);
}

static esp_err_t attiny_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_attiny, &reg, 1, val, 1, 100);
}

// ─── TC358762 DSI write ───────────────────────────────────────────────────────
static void tc_write(uint16_t reg, uint32_t val)
{
    esp_lcd_dsi_bus_t *bus = (esp_lcd_dsi_bus_t *)s_dsi_bus;
    uint8_t msg[6] = {
        reg & 0xFF,         (reg >> 8) & 0xFF,
        val & 0xFF,         (val >> 8) & 0xFF,
        (val >> 16) & 0xFF, (val >> 24) & 0xFF,
    };
    mipi_dsi_hal_host_gen_write_long_packet(
        &bus->hal, 0, MIPI_DSI_DT_GENERIC_LONG_WRITE, msg, sizeof(msg));
}

// ─── Step 1: I2C init ─────────────────────────────────────────────────────────
static esp_err_t s_i2c_init(const rpi_display_config_t *cfg)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = I2C_NUM_0,
        .sda_io_num        = cfg->sda_gpio,
        .scl_io_num        = cfg->scl_gpio,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus),
                        TAG, "I2C bus init failed");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = ATTINY_ADDR,
        .scl_speed_hz    = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_attiny),
                        TAG, "ATtiny add failed");
    return ESP_OK;
}

// ─── Step 2: ATtiny power-on (v1.1 regulator protocol) ───────────────────────
static esp_err_t s_attiny_power_on(void)
{
    uint8_t ver = 0;
    ESP_RETURN_ON_ERROR(attiny_read(REG_ID, &ver), TAG, "ATtiny read ID failed");
    ESP_LOGI(TAG, "ATtiny version: 0x%02x", ver);

    attiny_write(REG_PORTC, 0x00);         vTaskDelay(pdMS_TO_TICKS(10));
    attiny_write(REG_PORTA, PA_LCD_LR);    vTaskDelay(pdMS_TO_TICKS(10));
    attiny_write(REG_PORTB, PB_LCD_MAIN);  vTaskDelay(pdMS_TO_TICKS(10));
    attiny_write(REG_PORTC, PC_LED_EN);    vTaskDelay(pdMS_TO_TICKS(80));

    ESP_LOGI(TAG, "ATtiny power-on done (bridge in reset)");
    return ESP_OK;
}

// ─── Step 3: LDO ─────────────────────────────────────────────────────────────
static esp_err_t s_ldo_init(const rpi_display_config_t *cfg)
{
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id    = cfg->ldo_chan_id,
        .voltage_mv = cfg->ldo_voltage_mv,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &s_ldo),
                        TAG, "LDO init failed");
    return ESP_OK;
}

// ─── Step 4-7: DSI + DPI + TC358762 ──────────────────────────────────────────
static esp_err_t s_dsi_panel_init(const rpi_display_config_t *cfg)
{
    // DSI bus
    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id             = 0,
        .num_data_lanes     = cfg->dsi_lane_num,
        .phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = cfg->dsi_lane_mbps,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &s_dsi_bus),
                        TAG, "DSI bus init failed");

    // DBI IO — latches LP speed mode in DSI HW, then discard
    esp_lcd_panel_io_handle_t dbi_io = NULL;
    esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(s_dsi_bus, &dbi_cfg, &dbi_io),
                        TAG, "DBI IO init failed");
    esp_lcd_panel_io_del(dbi_io);

    // DPI panel
    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .num_fbs            = 2,
        .dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 25.98,
        .virtual_channel    = 0,
        .in_color_format    = LCD_COLOR_FMT_RGB888,
        .out_color_format   = LCD_COLOR_FMT_RGB888,
        .video_timing = {
            .h_size            = RPI_DISPLAY_WIDTH,
            .v_size            = RPI_DISPLAY_HEIGHT,
            .hsync_pulse_width = RPI_MODE_HSW,
            .hsync_back_porch  = RPI_MODE_HBP,
            .hsync_front_porch = RPI_MODE_HFP,
            .vsync_pulse_width = RPI_MODE_VSW,
            .vsync_back_porch  = RPI_MODE_VBP,
            .vsync_front_porch = RPI_MODE_VFP,
        },
        .flags.use_dma2d = false,
        .flags.disable_lp = 0,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_dpi(s_dsi_bus, &dpi_cfg, &s_panel),
                        TAG, "DPI panel init failed");

    // Override to NON-BURST (matches Linux TC358762 driver)
    {
        esp_lcd_dsi_bus_t *bus = (esp_lcd_dsi_bus_t *)s_dsi_bus;
        mipi_dsi_host_ll_dpi_set_video_burst_type(
            bus->hal.host, MIPI_DSI_LL_VIDEO_NON_BURST_WITH_SYNC_PULSES);
        mipi_dsi_host_ll_dpi_enable_frame_ack(bus->hal.host, false);
    }

    // Framebuffer
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(s_panel, 1, &s_fb),
                        TAG, "Get FB failed");
    memset(s_fb, 0x00, RPI_DISPLAY_WIDTH * RPI_DISPLAY_HEIGHT * 3);
    esp_cache_msync(s_fb, RPI_DISPLAY_WIDTH * RPI_DISPLAY_HEIGHT * 3,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M);

    // panel_init — starts HS clock and video stream
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel_init failed");

    // Force continuous HS clock + disable CMD ACK
    {
        esp_lcd_dsi_bus_t *bus = (esp_lcd_dsi_bus_t *)s_dsi_bus;
        mipi_dsi_host_ll_set_clock_lane_state(
            bus->hal.host, MIPI_DSI_LL_CLOCK_LANE_STATE_HS);
        mipi_dsi_host_ll_enable_cmd_ack(bus->hal.host, false);
    }

    // Release TC358762 reset + SYSPMCTRL=0 via ATtiny proxy
    attiny_write(REG_PORTC, PC_LED_EN | PC_RST_LCD_N | PC_RST_BRIDGE_N);
    vTaskDelay(pdMS_TO_TICKS(10));
    attiny_write(REG_ADDR_H,       0x04); vTaskDelay(pdMS_TO_TICKS(8));
    attiny_write(REG_ADDR_L,       0x7c); vTaskDelay(pdMS_TO_TICKS(8));
    attiny_write(REG_WRITE_DATA_H, 0x00); vTaskDelay(pdMS_TO_TICKS(8));
    attiny_write(REG_WRITE_DATA_L, 0x00); vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "TC358762 reset released");

    // TC358762 bridge init via DSI Generic Long Write
    tc_write(TC_DSI_LANEENABLE,       BIT(0) | BIT(1));
    tc_write(TC_PPI_D0S_CLRSIPOCOUNT, 0x05);
    tc_write(TC_PPI_D1S_CLRSIPOCOUNT, 0x05);
    tc_write(TC_PPI_D0S_ATMR,         0x00);
    tc_write(TC_PPI_D1S_ATMR,         0x00);
    tc_write(TC_PPI_LPTXTIMECNT,      0x03);
    tc_write(TC_SPICMR,               0x00);
    tc_write(TC_LCDCTRL,              0x00100150);
    tc_write(TC_SYSCTRL,              0x040f);
    tc_write(TC_LCD_HS_HBP,    (RPI_MODE_HBP << 16) | RPI_MODE_HSW);
    tc_write(TC_LCD_HDISP_HFP, (RPI_MODE_HFP << 16) | RPI_DISPLAY_WIDTH);
    tc_write(TC_LCD_VS_VBP,    (RPI_MODE_VBP << 16) | RPI_MODE_VSW);
    tc_write(TC_LCD_VDISP_VFP, (RPI_MODE_VFP << 16) | RPI_DISPLAY_HEIGHT);
    vTaskDelay(pdMS_TO_TICKS(100));
    tc_write(TC_PPI_STARTPPI,  0x01);
    tc_write(TC_DSI_STARTDSI,  0x01);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "TC358762 bridge init done");
    return ESP_OK;
}

// ─── Public API ───────────────────────────────────────────────────────────────
esp_err_t rpi_display_init(const rpi_display_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, TAG, "cfg is NULL");

    ESP_RETURN_ON_ERROR(s_i2c_init(cfg),        TAG, "I2C init failed");
    ESP_RETURN_ON_ERROR(s_attiny_power_on(),     TAG, "ATtiny power-on failed");
    ESP_RETURN_ON_ERROR(s_ldo_init(cfg),         TAG, "LDO init failed");
    ESP_RETURN_ON_ERROR(s_dsi_panel_init(cfg),   TAG, "DSI panel init failed");

    // Backlight on
    attiny_write(REG_PWM, 255);
    // Release touch reset
    attiny_write(REG_PORTC, PC_LED_EN | PC_RST_TP_N | PC_RST_LCD_N | PC_RST_BRIDGE_N);

    ESP_LOGI(TAG, "RPi 7\" display initialized");
    return ESP_OK;
}

esp_err_t rpi_display_deinit(void)
{
    if (s_panel)   { esp_lcd_panel_del(s_panel);      s_panel   = NULL; }
    if (s_dsi_bus) { esp_lcd_del_dsi_bus(s_dsi_bus);  s_dsi_bus = NULL; }
    if (s_ldo)     { esp_ldo_release_channel(s_ldo);  s_ldo     = NULL; }
    if (s_attiny)  { i2c_master_bus_rm_device(s_attiny); s_attiny = NULL; }
    if (s_i2c_bus) { i2c_del_master_bus(s_i2c_bus);   s_i2c_bus = NULL; }
    return ESP_OK;
}

esp_lcd_panel_handle_t rpi_display_get_panel(void)
{
    return s_panel;
}

esp_lcd_dsi_bus_handle_t rpi_display_get_dsi_bus(void)
{
    return s_dsi_bus;
}

void *rpi_display_get_framebuffer(void)
{
    return s_fb;
}

esp_err_t rpi_display_flush_framebuffer(void)
{
    if (!s_fb) return ESP_ERR_INVALID_STATE;
    return esp_cache_msync(s_fb,
                           RPI_DISPLAY_WIDTH * RPI_DISPLAY_HEIGHT * 3,
                           ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

esp_err_t rpi_display_set_brightness(uint8_t brightness)
{
    return attiny_write(REG_PWM, brightness);
}

i2c_master_bus_handle_t rpi_display_get_i2c_bus(void)
{
    return s_i2c_bus;
}
