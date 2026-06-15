#include "app_monitor_state.h"

#include "app_capture.h"
#include "app_dac.h"
#include "app_pwm.h"

void app_monitor_state_read(app_monitor_state_t *state)
{
    uint16_t i;
    uint16_t sample_count;

    if (state == 0) {
        return;
    }

    state->now_us = app_capture_get_time_us();
    state->pwm_output.frequency_hz = app_pwm_get_frequency();
    state->mains_frequency.frequency_x100 = app_capture_get_frequency_x100();
    state->dac_output.dual_output_enabled = app_dac_get_mode() == APP_DAC_MODE_DUAL ? 1U : 0U;
    state->dac_output.frequency_hz = app_dac_get_frequency();
    state->dac_output.amplitude = app_dac_get_amplitude();
    state->dac_output.phase_degrees = app_dac_get_phase();

    sample_count = app_dac_get_table_size();
    if (sample_count > APP_MONITOR_WAVEFORM_SAMPLES) {
        sample_count = APP_MONITOR_WAVEFORM_SAMPLES;
    }
    state->dac_output.waveform_sample_count = sample_count;

    for (i = 0U; i < sample_count; i++) {
        state->dac_output.waveform_ch1[i] = app_dac_get_sample(1U, i);
        state->dac_output.waveform_ch2[i] = app_dac_get_sample(2U, i);
    }

    for (; i < APP_MONITOR_WAVEFORM_SAMPLES; i++) {
        state->dac_output.waveform_ch1[i] = 2048U;
        state->dac_output.waveform_ch2[i] = 2048U;
    }
}
