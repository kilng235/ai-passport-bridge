#include <assert.h>
#include "brightness_logic.h"

int main(void)
{
    // is_valid:0(未写入)与越界值都视为无效,由调用方回退默认。
    assert(!brightness_is_valid(0));
    assert(!brightness_is_valid(1));
    assert(!brightness_is_valid(BRIGHTNESS_MIN_PERCENT - 1));
    assert(brightness_is_valid(BRIGHTNESS_MIN_PERCENT));
    assert(brightness_is_valid(50));
    assert(brightness_is_valid(BRIGHTNESS_MAX_PERCENT));
    assert(!brightness_is_valid(BRIGHTNESS_MAX_PERCENT + 1));

    // clamp:两侧收敛,界内原样。
    assert(brightness_clamp(-1) == BRIGHTNESS_MIN_PERCENT);
    assert(brightness_clamp(0) == BRIGHTNESS_MIN_PERCENT);
    assert(brightness_clamp(19) == BRIGHTNESS_MIN_PERCENT);
    assert(brightness_clamp(20) == 20);
    assert(brightness_clamp(73) == 73);
    assert(brightness_clamp(100) == 100);
    assert(brightness_clamp(101) == 100);
    assert(brightness_clamp(255) == 100);

    // step:上/下调一个步进,在边界停住,不做环绕。
    assert(brightness_step(BRIGHTNESS_MIN_PERCENT, +1) == BRIGHTNESS_MIN_PERCENT + BRIGHTNESS_STEP);
    assert(brightness_step(73, +1) == 83);
    assert(brightness_step(95, +1) == 100);
    assert(brightness_step(BRIGHTNESS_MAX_PERCENT, +1) == BRIGHTNESS_MAX_PERCENT);
    assert(brightness_step(BRIGHTNESS_MAX_PERCENT, -1) == BRIGHTNESS_MAX_PERCENT - BRIGHTNESS_STEP);
    assert(brightness_step(21, -1) == 20);
    assert(brightness_step(BRIGHTNESS_MIN_PERCENT, -1) == BRIGHTNESS_MIN_PERCENT);
    assert(brightness_step(50, 0) == 50);
    return 0;
}
