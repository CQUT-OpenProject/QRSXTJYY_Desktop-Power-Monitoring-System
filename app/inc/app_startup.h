/**
 * @file app_startup.h
 * @brief 上电启动编排接口。
 */
#ifndef APP_STARTUP_H
#define APP_STARTUP_H

/**
 * @brief 按上电安全顺序初始化板级资源、LCD 和应用模块。
 */
void app_startup_init(void);

#endif
