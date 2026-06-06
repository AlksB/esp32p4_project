#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GESTURE_NONE       = -1,
    GESTURE_ONE        =  0,  // "one"
    GESTURE_TWO        =  1,  // "two"
    GESTURE_THREE      =  2,  // "three"
    GESTURE_FOUR       =  3,  // "four"
    GESTURE_FIVE       =  4,  // "five"
    GESTURE_LIKE       =  5,  // "like"       👍
    GESTURE_OK         =  6,  // "ok"         👌
    GESTURE_NO_GESTURE =  7,  // "no_gesture"
    GESTURE_CALL       =  8,  // "call"       🤙
    GESTURE_DISLIKE    =  9,  // "dislike"    👎
    GESTURE_NO_HAND    = 10,  // "no_hand"
} gesture_label_t;

// Возвращает gesture_label_t или GESTURE_NONE если рука не детектирована
int  gesture_task_run(const uint8_t *rgb888, int width, int height);
void gesture_task_init(void);

#ifdef __cplusplus
}
#endif
