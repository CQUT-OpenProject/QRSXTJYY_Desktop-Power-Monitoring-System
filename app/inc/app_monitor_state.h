/**
 * @file app_monitor_state.h
 * @brief 监控状态快照接口。
 */
#ifndef APP_MONITOR_STATE_H
#define APP_MONITOR_STATE_H

#include "app_dac.h"

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
} app_monitor_state_t;

/**
 * @brief 读取一次供 LCD 和串口状态输出使用的监控快照。
 */
void app_monitor_state_read(app_monitor_state_t *state);

#endif
