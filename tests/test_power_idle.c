#include <assert.h>
#include "power_idle_logic.h"

int main(void)
{
    assert(power_idle_target(0) == POWER_IDLE_ON_PERCENT);
    assert(power_idle_target(POWER_IDLE_DIM_AFTER_US - 1) == POWER_IDLE_ON_PERCENT);
    assert(power_idle_target(POWER_IDLE_DIM_AFTER_US) == POWER_IDLE_DIM_PERCENT);
    assert(power_idle_target(POWER_IDLE_OFF_AFTER_US - 1) == POWER_IDLE_DIM_PERCENT);
    assert(power_idle_target(POWER_IDLE_OFF_AFTER_US) == 0);
    assert(power_idle_target(3600LL * 1000 * 1000) == 0);
    return 0;
}
