#include "app_screenshot.h"

#include "app_protocol.h"
#include "lcd.h"

#include <stdio.h>

/*
 * 传输契约（与上位机 tools/serial_cmd/src/bin/shot.rs 一致）：
 *   头帧：SHOT BEGIN W=<宽> H=<高> FMT=RLE16
 *   数据帧：纯大端十六进制 token 流，每 token 8 字符 = count(u16) + color(u16)
 *   尾帧：SHOT END PIX=<已编码像素总数>
 * 数据帧 payload 全为十六进制（永不含空格或 'S'），上位机据此区分控制帧。
 */

/* 每帧最多 7 个 token（7*8=56 字符 ≤ 63 payload 上限），且不跨帧拆 token。 */
#define SHOT_TOKENS_PER_LINE 7U
#define SHOT_LINE_BUF_LEN    ((SHOT_TOKENS_PER_LINE * 8U) + 1U)

static const char HEX_DIGITS[] = "0123456789ABCDEF";

/** 把一个 16 位值写成 4 个大端十六进制字符（不含 NUL）。 */
static void put_hex16(char *dst, uint16_t value)
{
    dst[0] = HEX_DIGITS[(value >> 12) & 0xFU];
    dst[1] = HEX_DIGITS[(value >> 8) & 0xFU];
    dst[2] = HEX_DIGITS[(value >> 4) & 0xFU];
    dst[3] = HEX_DIGITS[value & 0xFU];
}

void app_screenshot_dump(void)
{
    char line[SHOT_LINE_BUF_LEN];
    char ctrl[40];
    uint16_t w = lcddev.width;
    uint16_t h = lcddev.height;
    uint32_t emitted = 0U;   /* 已编码像素数，供尾帧校验 */
    uint16_t line_len = 0U;
    uint8_t tokens = 0U;
    uint16_t run_color = 0U;
    uint32_t run_count = 0U;
    uint8_t have_run = 0U;
    uint16_t x;
    uint16_t y;

    (void)snprintf(ctrl, sizeof(ctrl), "SHOT BEGIN W=%u H=%u FMT=RLE16",
                   (unsigned)w, (unsigned)h);
    app_protocol_send_shot_line(ctrl);

    for (y = 0U; y < h; y++) {
        for (x = 0U; x < w; x++) {
            uint16_t color = (uint16_t)LCD_ReadPoint(x, y);

            if (have_run == 0U) {
                run_color = color;
                run_count = 1U;
                have_run = 1U;
            } else if (color == run_color && run_count < 65535U) {
                run_count++;
            } else {
                /* 颜色变化或游程达上限：刷出一个 token，整 token 不跨帧 */
                put_hex16(&line[line_len], (uint16_t)run_count);
                put_hex16(&line[line_len + 4U], run_color);
                line_len = (uint16_t)(line_len + 8U);
                tokens++;
                emitted += run_count;
                if (tokens >= SHOT_TOKENS_PER_LINE) {
                    line[line_len] = '\0';
                    app_protocol_send_shot_line(line);
                    line_len = 0U;
                    tokens = 0U;
                }
                run_color = color;
                run_count = 1U;
            }
        }
    }

    /* 刷出最后一个游程及行缓冲残留 */
    if (have_run != 0U) {
        put_hex16(&line[line_len], (uint16_t)run_count);
        put_hex16(&line[line_len + 4U], run_color);
        line_len = (uint16_t)(line_len + 8U);
        emitted += run_count;
    }
    if (line_len > 0U) {
        line[line_len] = '\0';
        app_protocol_send_shot_line(line);
    }

    (void)snprintf(ctrl, sizeof(ctrl), "SHOT END PIX=%lu", (unsigned long)emitted);
    app_protocol_send_shot_line(ctrl);
}
