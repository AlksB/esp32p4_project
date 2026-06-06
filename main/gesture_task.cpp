#include "gesture_task.hpp"
#include "esp_log.h"
#include "hand_detect.hpp"
#include "hand_gesture_recognition.hpp"
#include <cstring>

static const char *TAG = "gesture";

static HandDetect *s_detector = nullptr;
static HandGestureRecognizer *s_recognizer = nullptr;

// Должен совпадать с hand_gesture_cat_names[] по индексам
static const char *s_cat_names[] = {"one",  "two",     "three",  "four",
                                    "five", "like",    "ok",     "no_gesture",
                                    "call", "dislike", "no_hand"};
static const int s_cat_count = sizeof(s_cat_names) / sizeof(s_cat_names[0]);

static int cat_name_to_label(const char *name) {
    if (!name)
        return (int)GESTURE_NONE;
    for (int i = 0; i < s_cat_count; i++) {
        if (strcmp(name, s_cat_names[i]) == 0)
            return i;
    }
    return (int)GESTURE_NONE;
}

extern "C" void gesture_task_init(void) {
    s_detector = new HandDetect();
    s_recognizer = new HandGestureRecognizer();
    ESP_LOGI(TAG, "Initialized");
}

extern "C" int gesture_task_run(const uint8_t *rgb888, int width, int height) {
    dl::image::img_t img = {
        .data = (void *)rgb888,
        .width = (uint16_t)width,
        .height = (uint16_t)height, // будет 224
        .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888,
    };

    auto detect_res = s_detector->run(img);
    ESP_LOGI(TAG, "detect count=%d", (int)detect_res.size());
    if (detect_res.empty()) {
        return (int)GESTURE_NONE;
    }

    auto cls_results = s_recognizer->recognize(img, detect_res);
    if (cls_results.empty()) {
        return (int)GESTURE_NONE;
    }

    const auto &best = cls_results[0];
    int label = cat_name_to_label(best.cat_name);
    ESP_LOGD(TAG, "cat=%s score=%.3f label=%d", best.cat_name, best.score,
             label);
    return label;
}
