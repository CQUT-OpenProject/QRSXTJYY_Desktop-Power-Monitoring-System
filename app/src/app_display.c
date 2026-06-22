#include "app_display.h"

#include "delay.h"
#include "lcd.h"

#include <stdio.h>

#define DISPLAY_CONTENT_BOTTOM 296U
#define DISPLAY_DA_GRAPH_X 16U
#define DISPLAY_DA_GRAPH_Y 132U
#define DISPLAY_DA_GRAPH_HEIGHT 120U
#define DISPLAY_DA_AXIS_TICK 4U

/* ponytail: 统一布局常量，各页面共享 */
#define UI_TITLE_Y        16U     /* 标题行 y */
#define UI_SEP_Y          36U     /* 标题下分隔线 y */
#define UI_CONTENT_Y      44U     /* 内容区起始 y */
#define UI_ITEM_STEP      28U     /* 菜单项行距 */
#define UI_BACK_Y         280U    /* "Back Home" 固定 y */
#define UI_LEFT           24U     /* 内容区左边距 */
#define UI_FONT_H         16U     /* 字体高度 */
#define UI_BAR_PAD        2U      /* 高亮条上下内边距 */
#define UI_HIGHLIGHT_BG   DARKBLUE  /* 选中项背景色 */

static app_ui_page_t s_rendered_page = APP_UI_PAGE_DASHBOARD;
static uint8_t s_has_rendered_page;
static uint8_t s_refresh_requested = 1U;

/* ponytail: forward decl, show_title needs it before definition */
static void draw_line_color(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);

/** 在 LCD 指定位置显示一行文本，支持自定义背景色。 */
static void show_text_line_bg(uint16_t x, uint16_t y, const char *text,
                              uint16_t color, uint16_t bg)
{
    POINT_COLOR = color;
    BACK_COLOR = bg;
    LCD_Fill(x, y, lcddev.width - 1U, (uint16_t)(y + UI_FONT_H - 1U), bg);
    LCD_ShowString(x, y, (uint16_t)(lcddev.width - x), UI_FONT_H, UI_FONT_H, (u8 *)text);
}

/** 在 LCD 指定位置显示一行文本（黑色背景）。 */
static void show_text_line(uint16_t x, uint16_t y, const char *text, uint16_t color)
{
    show_text_line_bg(x, y, text, color, BLACK);
}

/** 清除整个屏幕内容。 */
static void clear_content_area(void)
{
    LCD_Fill(0U, 0U, lcddev.width - 1U, lcddev.height - 1U, BLACK);
}

/** 显示页面标题 + 分隔线。 */
static void show_title(const char *text)
{
    show_text_line(10U, UI_TITLE_Y, text, CYAN);
    draw_line_color(0U, UI_SEP_Y, (uint16_t)(lcddev.width - 1U), UI_SEP_Y, GRAYBLUE);
}

/** 显示一行菜单项，选中时用深蓝高亮条 + 黄字，未选中黑底白字。 */
static void show_menu_item(uint8_t selected, uint16_t y, const char *text)
{
    uint16_t bar_top = (uint16_t)(y - UI_BAR_PAD);
    uint16_t bar_bot = (uint16_t)(y + UI_FONT_H + UI_BAR_PAD - 1U);
    uint16_t bg = selected != 0U ? UI_HIGHLIGHT_BG : BLACK;
    uint16_t fg = selected != 0U ? YELLOW : WHITE;

    LCD_Fill(0U, bar_top, lcddev.width - 1U, bar_bot, bg);
    show_text_line_bg(UI_LEFT, y, text, fg, bg);
}

/** 用指定颜色绘制一条线段。 */
static void draw_line_color(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    POINT_COLOR = color;
    LCD_DrawLine(x1, y1, x2, y2);
}

/** 将 12 位 DAC 样点 (0..4095) 映射到波形图 y 坐标。 */
static uint16_t map_dac_sample_to_y(uint16_t sample, uint16_t top, uint16_t height)
{
    uint32_t y;

    if (sample > 4095U) {
        sample = 4095U;
    }

    y = (uint32_t)(4095U - sample) * (uint32_t)(height - 1U);
    y = y / 4095U;
    return (uint16_t)(top + y);
}

/** 绘制一组 DAC 样点形成的波形曲线。 */
static void draw_dac_curve(const uint16_t *samples,
                           uint16_t count,
                           uint16_t left,
                           uint16_t top,
                           uint16_t width,
                           uint16_t height,
                           uint16_t color)
{
    uint16_t i;
    uint16_t previous_x;
    uint16_t previous_y;

    if (samples == 0 || count < 2U || width < 2U || height < 2U) {
        return;
    }

    previous_x = left;
    previous_y = map_dac_sample_to_y(samples[0], top, height);
    for (i = 1U; i < count; i++) {
        uint16_t x = (uint16_t)(left + (((uint32_t)i * (uint32_t)(width - 1U)) /
                                       (uint32_t)(count - 1U)));
        uint16_t y = map_dac_sample_to_y(samples[i], top, height);

        draw_line_color(previous_x, previous_y, x, y, color);
        previous_x = x;
        previous_y = y;
    }
}

/** 绘制 DAC 波形图坐标轴和刻度。 */
static void draw_dac_axes(uint16_t left,
                          uint16_t top,
                          uint16_t right,
                          uint16_t bottom,
                          uint16_t mid)
{
    uint16_t quarter_y = (uint16_t)(top + ((bottom - top) / 4U));
    uint16_t three_quarter_y = (uint16_t)(top + (((bottom - top) * 3U) / 4U));
    uint16_t x;

    draw_line_color(left, bottom, left, top, GRAY);
    draw_line_color(left, mid, right, mid, GRAY);

    draw_line_color(left, top, (uint16_t)(left - 3U), (uint16_t)(top + 5U), GRAY);
    draw_line_color(left, top, (uint16_t)(left + 3U), (uint16_t)(top + 5U), GRAY);

    draw_line_color(right, mid, (uint16_t)(right - 5U), (uint16_t)(mid - 3U), GRAY);
    draw_line_color(right, mid, (uint16_t)(right - 5U), (uint16_t)(mid + 3U), GRAY);

    draw_line_color((uint16_t)(left - DISPLAY_DA_AXIS_TICK),
                    quarter_y,
                    (uint16_t)(left + DISPLAY_DA_AXIS_TICK),
                    quarter_y,
                    GRAY);
    draw_line_color((uint16_t)(left - DISPLAY_DA_AXIS_TICK),
                    three_quarter_y,
                    (uint16_t)(left + DISPLAY_DA_AXIS_TICK),
                    three_quarter_y,
                    GRAY);

    for (x = (uint16_t)(left + 40U); x < right; x = (uint16_t)(x + 40U)) {
        draw_line_color(x,
                        (uint16_t)(mid - DISPLAY_DA_AXIS_TICK),
                        x,
                        (uint16_t)(mid + DISPLAY_DA_AXIS_TICK),
                        GRAYBLUE);
    }
}

/** 渲染主仪表盘：显示电参数 + ADC波形。 */
static void render_dashboard_page(const app_monitor_state_t *state, const app_ui_state_t *ui)
{
    char line1[32];
    char line2[32];
    const app_electrical_params_t *p = state->adc.params;
    uint16_t y = UI_CONTENT_Y;

    show_title("Power Dashboard");

    if (p == 0) {
        show_text_line(UI_LEFT, y, "No data", GRAY);
        show_menu_item(ui->cursor == 0U, UI_BACK_Y, "Enter Test Menu");
        return;
    }

    /* 文本参数两列显示 */
    snprintf(line1, sizeof(line1), "U:%3lu.%01luV",
             (unsigned long)(p->vrms_x100 / 100U),
             (unsigned long)((p->vrms_x100 % 100U) / 10U));
    snprintf(line2, sizeof(line2), "I:%lu.%03luA",
             (unsigned long)(p->irms_x1000 / 1000U),
             (unsigned long)(p->irms_x1000 % 1000U));
    show_text_line(10U, y, line1, YELLOW);
    show_text_line(120U, y, line2, CYAN);
    y = (uint16_t)(y + 20U);

    {
        int32_t pw = p->active_power_x10;
        uint32_t p_abs;
        const char *sign = "";
        if (pw < 0) {
            p_abs = (uint32_t)(-pw);
            sign = "-";
        } else {
            p_abs = (uint32_t)pw;
        }
        snprintf(line1, sizeof(line1), "P:%s%lu.%01luW",
                 sign,
                 (unsigned long)(p_abs / 10U),
                 (unsigned long)(p_abs % 10U));
    }
    snprintf(line2, sizeof(line2), "Lk:%lu.%01lumA",
             (unsigned long)(p->ilk_rms_x1000 / 1000U),
             (unsigned long)((p->ilk_rms_x1000 % 1000U) / 100U));
    show_text_line(10U, y, line1, WHITE);
    show_text_line(120U, y, line2, RED);
    y = (uint16_t)(y + 20U);

    snprintf(line1, sizeof(line1), "PF:%lu.%02lu",
             (unsigned long)(p->power_factor_x1000 / 1000U),
             (unsigned long)((p->power_factor_x1000 % 1000U) / 10U));
    snprintf(line2, sizeof(line2), "CAL:%s",
             p->zero_calibrated != 0U ? "Y" : "N");
    show_text_line(10U, y, line1, WHITE);
    show_text_line(120U, y, line2, GRAY);
    y = (uint16_t)(y + 24U);

    /* 绘制 ADC 波形 */
    {
        uint16_t graph_left = DISPLAY_DA_GRAPH_X;
        uint16_t graph_top = y;
        uint16_t graph_width = (uint16_t)(lcddev.width - DISPLAY_DA_GRAPH_X - 8U);
        uint16_t graph_height = (uint16_t)(UI_BACK_Y - y - 10U);
        uint16_t graph_right = (uint16_t)(graph_left + graph_width - 1U);
        uint16_t graph_bottom = (uint16_t)(graph_top + graph_height - 1U);
        uint16_t graph_mid = (uint16_t)(graph_top + (graph_height / 2U));

        LCD_Fill((uint16_t)(graph_left - DISPLAY_DA_AXIS_TICK),
                 graph_top,
                 graph_right,
                 graph_bottom,
                 BLACK);
        draw_dac_axes(graph_left, graph_top, graph_right, graph_bottom, graph_mid);

        if (state->adc.channels[0].samples != 0 && state->adc.channels[0].count > 1U) {
            draw_dac_curve(state->adc.channels[0].samples,
                           state->adc.channels[0].count,
                           (uint16_t)(graph_left + 1U),
                           graph_top,
                           (uint16_t)(graph_width - 2U),
                           graph_height,
                           YELLOW);
        }
        if (state->adc.channels[1].samples != 0 && state->adc.channels[1].count > 1U) {
            draw_dac_curve(state->adc.channels[1].samples,
                           state->adc.channels[1].count,
                           (uint16_t)(graph_left + 1U),
                           graph_top,
                           (uint16_t)(graph_width - 2U),
                           graph_height,
                           CYAN);
        }
    }

    show_menu_item(ui->cursor == 0U, UI_BACK_Y, "Enter Test Menu");
}

/** 渲染测试菜单页面。 */
static void render_test_menu(const app_ui_state_t *ui)
{
    uint16_t y = UI_CONTENT_Y;

    show_title("Test Menu");
    show_menu_item(ui->cursor == 0U, y, "Square Wave");
    y = (uint16_t)(y + UI_ITEM_STEP);
    show_menu_item(ui->cursor == 1U, y, "Freq Measure");
    y = (uint16_t)(y + UI_ITEM_STEP);
    show_menu_item(ui->cursor == 2U, y, "DA Wave Output");
    y = (uint16_t)(y + UI_ITEM_STEP);
    show_menu_item(ui->cursor == 3U, y, "System Info");
    y = (uint16_t)(y + UI_ITEM_STEP);
    show_menu_item(ui->cursor == 4U, y, "Back to Dashboard");
}

/** 渲染 PWM 频率设置页。编辑中显示草稿值，确认后才写入 TIM1。 */
static void render_pwm_page(const app_monitor_state_t *state, const app_ui_state_t *ui)
{
    char line[64];
    uint16_t freq_color;

    show_title("Square Wave");

    /* ponytail: 编辑态用 GREEN 区分，正常用高亮条自带颜色 */
    if (ui->editing != 0U && ui->cursor == 0U) {
        freq_color = GREEN;
    } else if (ui->cursor == 0U) {
        freq_color = YELLOW;
    } else {
        freq_color = WHITE;
    }

    snprintf(line, sizeof(line), "PWM Freq: %lu Hz %s",
             (unsigned long)(ui->editing != 0U ? ui->pwm_edit_frequency_hz : state->pwm_output.frequency_hz),
             ui->editing != 0U ? "[edit]" : "");
    {
        uint16_t bar_top = (uint16_t)(UI_CONTENT_Y - UI_BAR_PAD);
        uint16_t bar_bot = (uint16_t)(UI_CONTENT_Y + UI_FONT_H + UI_BAR_PAD - 1U);
        uint16_t bg = ui->cursor == 0U ? UI_HIGHLIGHT_BG : BLACK;
        LCD_Fill(0U, bar_top, lcddev.width - 1U, bar_bot, bg);
        show_text_line_bg(UI_LEFT, UI_CONTENT_Y, line, freq_color, bg);
    }

    show_menu_item(ui->cursor == 1U, (uint16_t)(UI_CONTENT_Y + UI_ITEM_STEP), "Back Home");
}

/** 渲染频率测量和串口上报设置页。 */
static void render_measure_page(const app_monitor_state_t *state, const app_ui_state_t *ui)
{
    char line[64];

    show_title("Freq Measure");

    snprintf(line, sizeof(line), "Serial Report: %s",
             ui->serial_auto_report_enabled != 0U ? "ON" : "OFF");
    {
        uint16_t bg = ui->cursor == 0U ? UI_HIGHLIGHT_BG : BLACK;
        uint16_t fg = ui->cursor == 0U ? YELLOW : WHITE;
        uint16_t bar_top = (uint16_t)(UI_CONTENT_Y - UI_BAR_PAD);
        uint16_t bar_bot = (uint16_t)(UI_CONTENT_Y + UI_FONT_H + UI_BAR_PAD - 1U);
        LCD_Fill(0U, bar_top, lcddev.width - 1U, bar_bot, bg);
        show_text_line_bg(UI_LEFT, UI_CONTENT_Y, line, fg, bg);
    }

    show_menu_item(ui->cursor == 1U, (uint16_t)(UI_CONTENT_Y + UI_ITEM_STEP), "Back Home");

    snprintf(line, sizeof(line), "PA1 Measure: %lu.%02lu Hz",
             (unsigned long)(state->mains_frequency.frequency_x100 / 100U),
             (unsigned long)(state->mains_frequency.frequency_x100 % 100U));
    show_text_line(10U, (uint16_t)(UI_CONTENT_Y + UI_ITEM_STEP * 3U), line, GREEN);
}

/** 渲染 DAC 波形监看页：配置参数 + CH1/CH2 波形曲线。 */
static void render_da_page(const app_monitor_state_t *state, const app_ui_state_t *ui)
{
    char line[64];
    uint16_t graph_left = DISPLAY_DA_GRAPH_X;
    uint16_t graph_top = DISPLAY_DA_GRAPH_Y;
    uint16_t graph_width = (uint16_t)(lcddev.width - DISPLAY_DA_GRAPH_X - 8U);
    uint16_t graph_height = DISPLAY_DA_GRAPH_HEIGHT;
    uint16_t graph_right = (uint16_t)(graph_left + graph_width - 1U);
    uint16_t graph_bottom = (uint16_t)(graph_top + graph_height - 1U);
    uint16_t graph_mid = (uint16_t)(graph_top + (graph_height / 2U));
    uint16_t count = state->dac_output.waveform_sample_count;

    if (count > APP_DAC_TABLE_SIZE) {
        count = APP_DAC_TABLE_SIZE;
    }

    show_title("DA Wave Output");

    snprintf(line, sizeof(line), "Mode:%s Freq:%luHz",
             state->dac_output.config.mode == APP_DAC_MODE_DUAL ? "DUAL" : "SINGLE",
             (unsigned long)state->dac_output.config.frequency_hz);
    show_text_line(10U, UI_CONTENT_Y, line, WHITE);

    snprintf(line, sizeof(line), "Amp:%u Phase:%u",
             state->dac_output.config.amplitude,
             state->dac_output.config.phase_degrees);
    show_text_line(10U, (uint16_t)(UI_CONTENT_Y + 22U), line, WHITE);

    show_text_line(10U, (uint16_t)(UI_CONTENT_Y + 44U), "CH1 Green  CH2 Yellow", GRAY);
    show_menu_item(ui->cursor == 0U, UI_BACK_Y, "Back Home");

    LCD_Fill((uint16_t)(graph_left - DISPLAY_DA_AXIS_TICK),
             graph_top,
             graph_right,
             graph_bottom,
             BLACK);
    draw_dac_axes(graph_left, graph_top, graph_right, graph_bottom, graph_mid);

    draw_dac_curve(state->dac_output.waveform_ch1,
                   count,
                   (uint16_t)(graph_left + 1U),
                   graph_top,
                   (uint16_t)(graph_width - 2U),
                   graph_height,
                   GREEN);
    draw_dac_curve(state->dac_output.waveform_ch2,
                   count,
                   (uint16_t)(graph_left + 1U),
                   graph_top,
                   (uint16_t)(graph_width - 2U),
                   graph_height,
                   YELLOW);
}

/** 渲染系统信息页。显示 USART 信息和固件版本号。 */
static void render_info_page(const app_ui_state_t *ui)
{
    char line[64];
    uint16_t y = UI_CONTENT_Y;

    show_title("System Info");
    snprintf(line, sizeof(line), "Firmware: %s", APP_FIRMWARE_VERSION);
    show_text_line(UI_LEFT, y, line, WHITE);
    y = (uint16_t)(y + UI_ITEM_STEP);
    show_text_line(UI_LEFT, y, "USART: USART1", WHITE);
    y = (uint16_t)(y + UI_ITEM_STEP);
    show_text_line(UI_LEFT, y, "Baudrate: 115200", WHITE);
    y = (uint16_t)(y + UI_ITEM_STEP);
    show_text_line(UI_LEFT, y, "Frame Config: 8N1", WHITE);

    show_menu_item(ui->cursor == 0U, UI_BACK_Y, "Back Home");
}

/** 根据当前页面刷新 LCD。换页时先清内容区避免残留。 */
static void refresh_values(const app_monitor_state_t *state, const app_ui_state_t *ui)
{
    if (s_has_rendered_page == 0U || s_rendered_page != ui->page) {
        clear_content_area();
        s_rendered_page = ui->page;
        s_has_rendered_page = 1U;
    }

    if (ui->page == APP_UI_PAGE_DASHBOARD) {
        render_dashboard_page(state, ui);
    } else if (ui->page == APP_UI_PAGE_PWM) {
        render_pwm_page(state, ui);
    } else if (ui->page == APP_UI_PAGE_MEASURE) {
        render_measure_page(state, ui);
    } else if (ui->page == APP_UI_PAGE_DA) {
        render_da_page(state, ui);
    } else if (ui->page == APP_UI_PAGE_INFO) {
        render_info_page(ui);
    } else {
        render_test_menu(ui);
    }
}

void app_display_init(void)
{
    LCD_Init();

    /* 红绿蓝快速闪屏，确认 LCD 和背光正常 */
    LCD_Clear(RED);
    delay_ms(120U);
    LCD_Clear(GREEN);
    delay_ms(120U);
    LCD_Clear(BLUE);
    delay_ms(120U);
    LCD_Clear(BLACK);

    s_has_rendered_page = 0U;
    s_refresh_requested = 1U;
}

/**
 * @brief 按事件请求更新 LCD 显示。
 */
void app_display_task(const app_monitor_state_t *state, const app_ui_state_t *ui)
{
    if (state == 0 || ui == 0) {
        return;
    }

    if (s_refresh_requested != 0U) {
        refresh_values(state, ui);
        s_refresh_requested = 0U;
    }
}

void app_display_request_refresh(void)
{
    s_refresh_requested = 1U;
}
