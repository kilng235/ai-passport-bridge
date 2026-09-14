#include <assert.h>
#include "power_idle_logic.h"

int main(void)
{
    // 默认 60s 测试:30s 调暗,60s 熄屏
    assert(power_idle_target(0) == POWER_IDLE_ON_PERCENT);
    assert(power_idle_target(29LL * 1000 * 1000) == POWER_IDLE_ON_PERCENT);
    assert(power_idle_target(30LL * 1000 * 1000) == POWER_IDLE_DIM_PERCENT);
    assert(power_idle_target(59LL * 1000 * 1000) == POWER_IDLE_DIM_PERCENT);
    assert(power_idle_target(60LL * 1000 * 1000) == 0);
    assert(power_idle_target(3600LL * 1000 * 1000) == 0);

    // 自定义 30s 测试:15s 调暗,30s 熄屏
    assert(power_idle_target_custom(14LL * 1000 * 1000, 30) == POWER_IDLE_ON_PERCENT);
    assert(power_idle_target_custom(15LL * 1000 * 1000, 30) == POWER_IDLE_DIM_PERCENT);
    assert(power_idle_target_custom(30LL * 1000 * 1000, 30) == 0);

    // 从不熄屏 (0s) 测试
    assert(power_idle_target_custom(0, 0) == POWER_IDLE_ON_PERCENT);
    assert(power_idle_target_custom(3600LL * 1000 * 1000, 0) == POWER_IDLE_ON_PERCENT);
    return 0;
}
