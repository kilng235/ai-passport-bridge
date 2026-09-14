#include "power_idle_logic.h"

uint8_t power_idle_target_custom(int64_t idle_us, uint16_t off_sec)
{
    if (off_sec == 0) return POWER_IDLE_ON_PERCENT;   // 从不熄屏

    int64_t off_us = (int64_t)off_sec * 1000 * 1000;
    int64_t dim_us = off_us / 2;

    if (idle_us >= off_us) return 0;
    if (idle_us >= dim_us) return POWER_IDLE_DIM_PERCENT;
    return POWER_IDLE_ON_PERCENT;
}

uint8_t power_idle_target(int64_t idle_us)
{
    return power_idle_target_custom(idle_us, POWER_IDLE_TIMEOUT_60S);
}
