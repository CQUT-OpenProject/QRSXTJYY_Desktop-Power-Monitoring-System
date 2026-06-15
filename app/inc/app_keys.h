/**
 * @file app_keys.h
 * @brief Board key scan interface.
 */
#ifndef APP_KEYS_H
#define APP_KEYS_H

#include <stdint.h>

typedef enum {
    /** 没有新的稳定按键事件。 */
    APP_KEY_EVENT_NONE = 0,

    /** 菜单向上移动，来自 KEY1。 */
    APP_KEY_EVENT_UP,

    /** 菜单向下移动，来自 KEY0。 */
    APP_KEY_EVENT_DOWN,

    /** 确认/进入，来自 KEYUP。 */
    APP_KEY_EVENT_CONFIRM
} app_key_event_t;

/**
 * @brief 初始化三个板载按键使用的 GPIO。
 */
void app_keys_init(void);

/**
 * @brief 扫描按键并做简单消抖。
 *
 * @param now_us 当前微秒时间戳，由输入捕获模块的 TIM2 提供。
 * @return 只有按键状态稳定变化后才返回非 NONE 事件。
 */
app_key_event_t app_keys_poll(uint32_t now_us);

#endif
