#include "power_idle_logic.h"

uint8_t power_idle_target(int64_t idle_us)
{
    if (idle_us >= POWER_IDLE_OFF_AFTER_US) return 0;
    if (idle_us >= POWER_IDLE_DIM_AFTER_US) return POWER_IDLE_DIM_PERCENT;
    return POWER_IDLE_ON_PERCENT;
}
