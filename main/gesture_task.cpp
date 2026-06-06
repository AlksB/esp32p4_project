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

void gesture_task_init(void) {
    s_detector = new HandDetect();
    s_recognizer = new HandGestureRecognizer();
    ESP_LOGI(TAG, "Initialized");
}

gesture_result_t gesture_task_run(const uint8_t *rgb888, int width,
                                  int height) {
    gesture_result_t result = {};
    result.label = (int)GESTURE_NONE;

    dl::image::img_t img = {
        .data = (void *)rgb888,
        .width = (uint16_t)width,
        .height = (uint16_t)height,
        .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888,
    };

    auto detect_res = s_detector->run(img);
    if (detect_res.empty()) {
        return result;
    }

    // bbox руки в координатах 224×224
    const auto &hand = detect_res.front();
    result.has_hand = true;
    result.hand_x1 = hand.box[0] * 800 / 224;
    result.hand_y1 = hand.box[1] * 480 / 224;
    result.hand_x2 = hand.box[2] * 800 / 224;
    result.hand_y2 = hand.box[3] * 480 / 224;

    // Классификация жеста
    auto cls_results = s_recognizer->recognize(img, detect_res);
    if (!cls_results.empty()) {
        const auto &best = cls_results[0];
        result.label = cat_name_to_label(best.cat_name);
        result.score = best.score;
        result.has_gesture = true;

        // bbox жеста совпадает с bbox руки — классификатор работает внутри него
        // но можно немного уменьшить для визуального различия
        int margin = (result.hand_x2 - result.hand_x1) / 10;
        result.gesture_x1 = result.hand_x1 + margin;
        result.gesture_y1 = result.hand_y1 + margin;
        result.gesture_x2 = result.hand_x2 - margin;
        result.gesture_y2 = result.hand_y2 - margin;

        ESP_LOGI(TAG, "cat=%s score=%.3f", best.cat_name, best.score);
    }

    return result;
}
