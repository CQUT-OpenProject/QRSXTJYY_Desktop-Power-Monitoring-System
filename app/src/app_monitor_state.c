#include "app_monitor_state.h"

#include "app_capture.h"
#include "app_dac.h"
#include "app_pwm.h"

void app_monitor_state_read(app_monitor_state_t *state)
{
    if (state == 0) {
        return;
    }

    /*
     * 把几个模块的状态抄到同一份快照里。显示和串口只看这份结构体，
     * 不必分别去找输入捕获、PWM 和 DAC 的 getter。
     */
    state->now_us = app_capture_get_time_us();

    /* PWM 输出频率来自 TIM1 模块。 */
    state->pwm_output.frequency_hz = app_pwm_get_frequency();

    /* 市电频率来自 TIM2 输入捕获模块，单位是 Hz * 100。 */
    state->mains_frequency.frequency_x100 = app_capture_get_frequency_x100();

    /* DAC 输出状态已经包含配置和预览波形，这里直接复制进快照。 */
    app_dac_read_output(&state->dac_output);
}
