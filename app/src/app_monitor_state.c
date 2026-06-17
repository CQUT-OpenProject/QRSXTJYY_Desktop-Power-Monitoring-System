#include "app_monitor_state.h"

#include "app_adc.h"
#include "app_capture.h"
#include "app_dac.h"
#include "app_pwm.h"

/**
 * @brief 汇总各模块状态到统一监控快照。
 *
 * PWM、捕获、DAC、ADC 各模块的状态集中到一份快照中，
 * LCD 和串口只需读取此结构体，不必分别调用各模块 getter。
 * ADC 样点持有指针而非复制数据。
 */
void app_monitor_state_read(app_monitor_state_t *state)
{
    if (state == 0) {
        return;
    }

    state->now_us = app_capture_get_time_us();
    state->pwm_output.frequency_hz = app_pwm_get_frequency();
    state->mains_frequency.frequency_x100 = app_capture_get_frequency_x100();
    app_dac_read_output(&state->dac_output);

    {
        uint8_t ch;
        for (ch = 0U; ch < 3U; ch++) {
            state->adc.channels[ch].samples = app_adc_get_samples(ch);
            state->adc.channels[ch].count   = APP_ADC_SAMPLES;
        }
    }
    state->adc.params = app_adc_get_params();
}
