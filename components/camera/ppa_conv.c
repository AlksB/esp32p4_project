#include "ppa_conv.h"
#include "driver/ppa.h"
#include "esp_log.h"
#include "esp_cache.h"
#include "esp_check.h"

static const char *TAG = "ppa_conv";

static ppa_client_handle_t s_ppa_srm = NULL;

esp_err_t ppa_conv_init(void)
{
    ppa_client_config_t cfg = {
        .oper_type = PPA_OPERATION_SRM,
    };
    ESP_RETURN_ON_ERROR(ppa_register_client(&cfg, &s_ppa_srm),
                        TAG, "PPA SRM register failed");
    ESP_LOGI(TAG, "PPA SRM client registered");
    return ESP_OK;
}

esp_err_t ppa_conv_rgb565_to_rgb888(const void *src, uint32_t src_w, uint32_t src_h,
                                     void *dst, uint32_t dst_w, uint32_t dst_h)
{
    if (!s_ppa_srm) return ESP_ERR_INVALID_STATE;

    // Sync src cache before PPA reads it
    esp_cache_msync((void *)src, src_w * src_h * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

    ppa_srm_oper_config_t srm_cfg = {
        .in = {
            .buffer      = (void *)src,
            .pic_w        = src_w,
            .pic_h        = src_h,
            .block_w      = dst_w,           // crop по ширине
            .block_h      = dst_h,           // crop по высоте (480 из 640)
            .block_offset_x = 0,
            .block_offset_y = (src_h - dst_h) / 2, // центрируем по высоте
            .srm_cm       = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer      = dst,
            .buffer_size = dst_w * dst_h * 3,
            .pic_w        = dst_w,
            .pic_h        = dst_h,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm       = PPA_SRM_COLOR_MODE_RGB888,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x        = 1.0f,
        .scale_y        = 1.0f,
        .mirror_x       = false,
        .mirror_y       = false,
        .rgb_swap       = true,
        .byte_swap      = false,
        .mode           = PPA_TRANS_MODE_BLOCKING,
    };

    ESP_RETURN_ON_ERROR(ppa_do_scale_rotate_mirror(s_ppa_srm, &srm_cfg),
                        TAG, "PPA SRM failed");

    // Sync dst cache after PPA writes
    esp_cache_msync(dst, dst_w * dst_h * 3, ESP_CACHE_MSYNC_FLAG_DIR_M2C);

    return ESP_OK;
}

esp_err_t ppa_conv_deinit(void)
{
    if (s_ppa_srm) {
        ppa_unregister_client(s_ppa_srm);
        s_ppa_srm = NULL;
    }
    return ESP_OK;
}
