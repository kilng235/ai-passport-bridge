#include "brightness_logic.h"

bool brightness_is_valid(int percent)
{
    return percent >= BRIGHTNESS_MIN_PERCENT && percent <= BRIGHTNESS_MAX_PERCENT;
}

uint8_t brightness_clamp(int percent)
{
    if (percent < BRIGHTNESS_MIN_PERCENT) return BRIGHTNESS_MIN_PERCENT;
    if (percent > BRIGHTNESS_MAX_PERCENT) return BRIGHTNESS_MAX_PERCENT;
    return (uint8_t)percent;
}

uint8_t brightness_step(uint8_t current, int dir)
{
    if (dir > 0) return brightness_clamp(current + BRIGHTNESS_STEP);
    if (dir < 0) return brightness_clamp(current - BRIGHTNESS_STEP);
    return brightness_clamp(current);
}
