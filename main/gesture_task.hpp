#pragma once
#include <cstdint>

typedef enum {
    GESTURE_NONE = -1,
    GESTURE_ONE = 0,
    GESTURE_TWO = 1,
    GESTURE_THREE = 2,
    GESTURE_FOUR = 3,
    GESTURE_FIVE = 4,
    GESTURE_LIKE = 5,
    GESTURE_OK = 6,
    GESTURE_NO_GESTURE = 7,
    GESTURE_CALL = 8,
    GESTURE_DISLIKE = 9,
    GESTURE_NO_HAND = 10,
} gesture_label_t;

typedef struct {
    int label;
    int x1, y1, x2, y2; // координаты в пикселях дисплея (800×480)
    bool has_hand;
} gesture_result_t;

void gesture_task_init(void);
gesture_result_t gesture_task_run(const uint8_t *rgb888, int width, int height);
