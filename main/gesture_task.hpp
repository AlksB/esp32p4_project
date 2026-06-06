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
    // bbox руки от детектора (координаты 224×224 → пересчитываются в main)
    int hand_x1, hand_y1, hand_x2, hand_y2;
    bool has_hand;

    // bbox жеста = тот же bbox что и рука (классификатор работает внутри него)
    // но оставляем отдельно для возможности независимой отрисовки
    int gesture_x1, gesture_y1, gesture_x2, gesture_y2;
    int label;
    float score;
    bool has_gesture;
} gesture_result_t;

void gesture_task_init(void);
gesture_result_t gesture_task_run(const uint8_t *rgb888, int width, int height);
