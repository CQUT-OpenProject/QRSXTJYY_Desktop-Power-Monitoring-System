/**
 * @file app_relay.h
 * @brief miniIO 副板继电器控制接口。
 *
 * 控制引脚：PC12（高电平有效）。
 * miniIO 副板 relay 信号接 JP1-11/12，高电平吸合，低电平释放。
 *
 * 使用规则：
 *   - 上电后由 app_relay_init() 将 PC12 配置为推挽输出并默认关闭（防误动作）。
 *   - UI 状态切换时按页面调用 app_relay_set()，不要在 LCD 绘图函数内重复调用。
 *   - Dashboard 页面：app_relay_set(1)  — 继电器吸合。
 *   - Test 菜单及所有子页面：app_relay_set(0) — 继电器释放。
 */
#ifndef APP_RELAY_H
#define APP_RELAY_H

#include <stdint.h>

/**
 * @brief 初始化继电器控制 GPIO（PC12，推挽输出，默认低电平关闭）。
 *
 * 应在板级初始化完成、UI 主循环启动之前调用。
 */
void app_relay_init(void);

/**
 * @brief 设置继电器状态。
 *
 * @param on  非零值：闭合（PC12 高电平）；0：断开（PC12 低电平）。
 */
void app_relay_set(uint8_t on);

/**
 * @brief 获取当前继电器状态。
 *
 * @return 1 表示继电器已闭合，0 表示已断开。
 */
uint8_t app_relay_get(void);

#endif /* APP_RELAY_H */
