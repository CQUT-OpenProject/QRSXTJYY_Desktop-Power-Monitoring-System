/**
 * @file app_keys.h
 * @brief 板载按键扫描接口。
 */
#ifndef APP_KEYS_H
#define APP_KEYS_H

#include <stdint.h>

typedef enum {
    APP_KEY_EVENT_NONE = 0,     /**< 无新事件。 */
    APP_KEY_EVENT_UP,           /**< 菜单向上，来自 KEYUP(PA0)。 */
    APP_KEY_EVENT_DOWN,         /**< 菜单向下，来自 KEY1(PA15)。 */
    APP_KEY_EVENT_CONFIRM       /**< 确认/进入，来自 KEY0(PC5)。 */
} app_key_event_t;

/**
 * @brief 初始化三个按键 GPIO。
 *
 * KEY1/KEY0 低有效上拉输入；KEYUP 高有效下拉输入。
 */
void app_keys_init(void);

/**
 * @brief 扫描按键并返回消抖后的事件。
 *
 * @param now_us 当前微秒时间戳，用于 20 ms 消抖窗口。
 * @return 电平稳定变化后才返回非 NONE 事件。
 */
app_key_event_t app_keys_poll(uint32_t now_us);

#endif
