/**
 * @file app_screenshot.h
 * @brief 临时屏幕截图：回读 LCD GDDRAM，RLE 压缩后经串口协议发送。
 *
 * 临时调试功能，便于整体移除。详见 app_screenshot.c。
 */
#ifndef APP_SCREENSHOT_H
#define APP_SCREENSHOT_H

/**
 * @brief 截取当前 LCD 画面并通过 USART1 发送。
 *
 * 逐像素回读 RGB565，做行程编码（RLE），编码为大端十六进制文本，
 * 分多帧（TYPE=0x84）发送：头帧 "SHOT BEGIN ..."、若干十六进制数据帧、
 * 尾帧 "SHOT END PIX=..."。
 *
 * 阻塞执行，期间主循环冻结（无看门狗，安全），传完自动恢复。
 */
void app_screenshot_dump(void);

#endif /* APP_SCREENSHOT_H */
