/**
 * @file app_monitor_state.h
 * @brief 监控状态快照接口。
 */
#ifndef APP_MONITOR_STATE_H
#define APP_MONITOR_STATE_H

#include "app_dac.h"
#include "app_adc.h"

#include <stdint.h>

typedef struct {
    /** 市电频率，单位是 Hz * 100，例如 5000 表示 50.00 Hz。 */
    uint32_t frequency_x100;
} app_monitor_mains_frequency_t;

typedef struct {
    /** PA8/TIM1_CH1 当前设置的 PWM 测试频率，单位 Hz。 */
    uint32_t frequency_hz;
} app_monitor_pwm_output_t;

/**
 * @brief 单个 ADC 通道的样点引用。
 *
 * 不复制数据，只持有指针和长度，指向 app_adc 模块内的静态数组。
 */
typedef struct {
    /** 指向通道样点数组的指针（128 半字）。 */
    const uint16_t *samples;
    /** 样点数（固定为 APP_ADC_SAMPLES）。 */
    uint16_t count;
} app_monitor_adc_channel_t;

/**
 * @brief ADC 采样和电参数汇总，供 LCD 和串口读取。
 */
typedef struct {
    /** 三通道（VL / iL / iLK）原始样点引用。 */
    app_monitor_adc_channel_t channels[3];
    /** 最近一次计算得到的电参数（只读指针）。 */
    const app_electrical_params_t *params;
} app_monitor_adc_t;

/**
 * @brief LCD 和串口共同读取的一份监控快照。
 *
 * 这里把 PWM、输入捕获和 DAC 的状态放在一起。LCD 和 STATUS? 都从这份
 * 快照取数，查问题时不用到处找字段来自哪里。
 */
typedef struct {
    /** 当前微秒时间戳，用来控制显示刷新节奏。 */
    uint32_t now_us;

    /** PA1 输入捕获得到的市电频率测量结果。 */
    app_monitor_mains_frequency_t mains_frequency;

    /** PA8 PWM 测试频率源状态。 */
    app_monitor_pwm_output_t pwm_output;

    /** PA4/PA5 DAC 波形输出状态和波形预览。 */
    app_dac_output_t dac_output;

    /** PC0/PC1/PC2 ADC 采样与电参数。 */
    app_monitor_adc_t adc;
} app_monitor_state_t;

/**
 * @brief 读取一次供 LCD 和串口状态输出使用的监控快照。
 */
void app_monitor_state_read(app_monitor_state_t *state);

#endif
