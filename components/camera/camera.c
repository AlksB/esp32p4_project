#include "camera.h"

#include "driver/isp.h"
#include "esp_cache.h"
#include "esp_cam_ctlr.h"
#include "esp_cam_ctlr_csi.h"
#include "esp_cam_sensor.h"
#include "esp_cam_sensor_detect.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sccb_i2c.h"
#include "esp_sccb_intf.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "camera";

#define CAM_FORMAT "MIPI_2lane_24Minput_RAW8_800x640_50fps"
#define SCCB_FREQ 100000

static esp_cam_ctlr_handle_t s_cam_handle = NULL;
static isp_proc_handle_t s_isp_proc = NULL;
static void *s_fb = NULL;

static esp_cam_ctlr_trans_t s_trans;

static SemaphoreHandle_t s_frame_sem = NULL;

static bool s_on_get_new_trans(esp_cam_ctlr_handle_t handle,
                                esp_cam_ctlr_trans_t *trans, void *user_data)
{
    // Переподаём тот же буфер для следующего кадра
    trans->buffer = s_fb;
    trans->buflen = CAMERA_FB_SIZE;
    return false;
}

static bool s_on_trans_finished(esp_cam_ctlr_handle_t handle,
                                 esp_cam_ctlr_trans_t *trans, void *user_data)
{
    esp_cache_msync(s_fb, CAMERA_FB_SIZE, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    // Сигнализируем что кадр готов
    BaseType_t high_task_woken = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)user_data, &high_task_woken);
    return high_task_woken == pdTRUE;
}

esp_err_t camera_init(i2c_master_bus_handle_t i2c_bus, void *fb) {
    esp_err_t ret;
    s_fb = fb;

    // ── 1. Инициализация сенсора через SCCB ──────────────────────────────────
    esp_cam_sensor_config_t cam_cfg = {
        .reset_pin = -1,
        .pwdn_pin = -1,
        .xclk_pin = -1,
    };

    esp_cam_sensor_device_t *cam = NULL;
    for (esp_cam_sensor_detect_fn_t *p =
             &__esp_cam_sensor_detect_fn_array_start;
         p < &__esp_cam_sensor_detect_fn_array_end; ++p) {

        sccb_i2c_config_t i2c_cfg = {
            .scl_speed_hz = SCCB_FREQ,
            .device_address = p->sccb_addr,
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        };
        ret = sccb_new_i2c_io(i2c_bus, &i2c_cfg, &cam_cfg.sccb_handle);
        if (ret != ESP_OK)
            continue;

        cam_cfg.sensor_port = p->port;
        cam = (*(p->detect))(&cam_cfg);
        if (cam) {
            if (p->port != ESP_CAM_SENSOR_MIPI_CSI) {
                ESP_LOGE(TAG, "Wrong sensor interface");
                return ESP_ERR_NOT_SUPPORTED;
            }
            break;
        }
        esp_sccb_del_i2c_io(cam_cfg.sccb_handle);
    }

    if (!cam) {
        ESP_LOGE(TAG, "Camera sensor not detected");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "Camera sensor detected");

    // Установить формат
    esp_cam_sensor_format_array_t fmt_array = {0};
    esp_cam_sensor_query_format(cam, &fmt_array);

    const esp_cam_sensor_format_t *selected = NULL;
    for (int i = 0; i < fmt_array.count; i++) {
        ESP_LOGI(TAG, "fmt[%d]: %s", i, fmt_array.format_array[i].name);
        if (!strcmp(fmt_array.format_array[i].name, CAM_FORMAT)) {
            selected = &fmt_array.format_array[i];
        }
    }
    if (!selected) {
        ESP_LOGE(TAG, "Format not found: %s", CAM_FORMAT);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_ERROR_CHECK(esp_cam_sensor_set_format(cam, selected));
    ESP_LOGI(TAG, "Format set: %s", selected->name);

    int stream = 1;
    ESP_ERROR_CHECK(
        esp_cam_sensor_ioctl(cam, ESP_CAM_SENSOR_IOC_S_STREAM, &stream));

    // ── 2. CSI controller ────────────────────────────────────────────────────
    esp_cam_ctlr_csi_config_t csi_cfg = {
        .ctlr_id = 0,
        .h_res = CAMERA_H_RES,
        .v_res = CAMERA_V_RES,
        .lane_bit_rate_mbps = CAMERA_LANE_BITRATE,
        .input_data_color_type = CAM_CTLR_COLOR_RAW8,
        .output_data_color_type = CAM_CTLR_COLOR_RGB565,
        .data_lane_num = 2,
        .byte_swap_en = false,
        .queue_items = 1,
    };
    ESP_ERROR_CHECK(esp_cam_new_csi_ctlr(&csi_cfg, &s_cam_handle));

    s_frame_sem = xSemaphoreCreateBinary();
    
    esp_cam_ctlr_evt_cbs_t cbs = {
        .on_get_new_trans  = s_on_get_new_trans,
        .on_trans_finished = s_on_trans_finished,
    };
    ESP_ERROR_CHECK(esp_cam_ctlr_register_event_callbacks(s_cam_handle, &cbs, s_frame_sem));
    ESP_ERROR_CHECK(esp_cam_ctlr_enable(s_cam_handle));

    // ── 3. ISP ───────────────────────────────────────────────────────────────
    esp_isp_processor_cfg_t isp_cfg = {
        .clk_hz = 80 * 1000 * 1000,
        .input_data_source = ISP_INPUT_DATA_SOURCE_CSI,
        .input_data_color_type = ISP_COLOR_RAW8,
        .output_data_color_type = ISP_COLOR_RGB565,
        .has_line_start_packet = false,
        .has_line_end_packet = false,
        .h_res = CAMERA_H_RES,
        .v_res = CAMERA_V_RES,
    };
    ESP_ERROR_CHECK(esp_isp_new_processor(&isp_cfg, &s_isp_proc));
    ESP_ERROR_CHECK(esp_isp_enable(s_isp_proc));

    // ── 4. Старт ─────────────────────────────────────────────────────────────
    s_trans.buffer = s_fb;
    s_trans.buflen = CAMERA_FB_SIZE;

    ESP_ERROR_CHECK(esp_cam_ctlr_start(s_cam_handle));
    ESP_LOGI(TAG, "Camera started");

    return ESP_OK;
}

esp_err_t camera_get_frame(void)
{
    if (xSemaphoreTake(s_frame_sem, pdMS_TO_TICKS(1000)) == pdTRUE) {
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t camera_deinit(void) {
    if (s_cam_handle) {
        esp_cam_ctlr_disable(s_cam_handle);
        esp_cam_ctlr_del(s_cam_handle);
        s_cam_handle = NULL;
    }
    if (s_isp_proc) {
        esp_isp_disable(s_isp_proc);
        esp_isp_del_processor(s_isp_proc);
        s_isp_proc = NULL;
    }
    return ESP_OK;
}
