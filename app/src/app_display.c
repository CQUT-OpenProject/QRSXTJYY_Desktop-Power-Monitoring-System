#include "app_display.h"

#include "delay.h"
#include "lcd.h"

#include <stdio.h>

#define DISPLAY_REFRESH_US 500000U
#define DISPLAY_CONTENT_BOTTOM 296U
#define DISPLAY_DA_GRAPH_X 16U
#define DISPLAY_DA_GRAPH_Y 132U
#define DISPLAY_DA_GRAPH_HEIGHT 120U
#define DISPLAY_DA_AXIS_TICK 4U

// 主循环或 UI 改变状态时置 1，下一次 display_task 会重画。
static uint8_t s_refresh_requested = 1U;

// 记住上一次画过的页面，换页时要先清内容区。
static app_ui_page_t s_rendered_page = APP_UI_PAGE_MENU;
// 是否已经至少完成过一次页面渲染
static uint8_t s_has_rendered_page;

// 上次刷新时间，防止 LCD 刷新太频繁造成闪烁。
static uint32_t s_last_refresh_us;

/**
 * @brief 在 LCD 指定位置显示一行文本。
 */
static void show_text_line(uint16_t x, uint16_t y, const char *text, uint16_t color)
{
    // ALIENTEK LCD 库用全局 POINT_COLOR/BACK_COLOR 控制文字颜色。
    // 每次显示一行前先清掉这一行背景，避免旧字符残留。
    POINT_COLOR = color;
    BACK_COLOR = BLACK;
    LCD_Fill(x, y, lcddev.width - 1U, (uint16_t)(y + 15U), BLACK);
    LCD_ShowString(x, y, (uint16_t)(lcddev.width - x), 16U, 16U, (u8 *)text);
}

/**
 * @brief 清除 LCD 内容区域并保留底部提示栏。
 */
static void clear_content_area(void)
{
    // 只清内容区，底部串口提示栏保留不动。
    LCD_Fill(0U, 0U, lcddev.width - 1U, DISPLAY_CONTENT_BOTTOM, BLACK);
}

/**
 * @brief 显示一行带选中光标的菜单项。
 */
static void show_menu_item(uint8_t selected, uint16_t y, const char *text)
{
    // 带光标前缀的菜单行文本
    char line[48];

    // 用 '>' 标出当前光标位置，LCD 上也很直观。
    snprintf(line, sizeof(line), "%c %s", selected != 0U ? '>' : ' ', text);
    show_text_line(18U, y, line, selected != 0U ? YELLOW : WHITE);
}

/**
 * @brief 使用指定颜色绘制一条 LCD 线段。
 */
static void draw_line_color(uint16_t x1,
                            uint16_t y1,
                            uint16_t x2,
                            uint16_t y2,
                            uint16_t color)
{
    POINT_COLOR = color;
    LCD_DrawLine(x1, y1, x2, y2);
}

/**
 * @brief 将 12 位 DAC 样点映射到波形图的 y 坐标。
 */
static uint16_t map_dac_sample_to_y(uint16_t sample, uint16_t top, uint16_t height)
{
    // 样点映射到图形区域内后的纵向偏移
    uint32_t y;

    if (sample > 4095U) {
        sample = 4095U;
    }

    y = (uint32_t)(4095U - sample) * (uint32_t)(height - 1U);
    y = y / 4095U;
    return (uint16_t)(top + y);
}

/**
 * @brief 绘制一组 DAC 样点形成的波形曲线。
 */
static void draw_dac_curve(const uint16_t *samples,
                           uint16_t count,
                           uint16_t left,
                           uint16_t top,
                           uint16_t width,
                           uint16_t height,
                           uint16_t color)
{
    // 当前绘制的波形点下标
    uint16_t i;
    // 上一个样点映射后的屏幕 x 坐标
    uint16_t previous_x;
    // 上一个样点映射后的屏幕 y 坐标
    uint16_t previous_y;

    if (samples == 0 || count < 2U || width < 2U || height < 2U) {
        return;
    }

    previous_x = left;
    previous_y = map_dac_sample_to_y(samples[0], top, height);
    for (i = 1U; i < count; i++) {
        // 当前样点映射后的屏幕 x 坐标
        uint16_t x = (uint16_t)(left + (((uint32_t)i * (uint32_t)(width - 1U)) /
                                       (uint32_t)(count - 1U)));
        // 当前样点映射后的屏幕 y 坐标
        uint16_t y = map_dac_sample_to_y(samples[i], top, height);

        draw_line_color(previous_x, previous_y, x, y, color);
        previous_x = x;
        previous_y = y;
    }
}

/**
 * @brief 绘制 DAC 波形图坐标轴和刻度。
 */
static void draw_dac_axes(uint16_t left,
                          uint16_t top,
                          uint16_t right,
                          uint16_t bottom,
                          uint16_t mid)
{
    // 图形区域 1/4 高度处的刻度 y 坐标
    uint16_t quarter_y = (uint16_t)(top + ((bottom - top) / 4U));
    // 图形区域 3/4 高度处的刻度 y 坐标
    uint16_t three_quarter_y = (uint16_t)(top + (((bottom - top) * 3U) / 4U));
    // 横轴刻度绘制时使用的 x 坐标
    uint16_t x;

    // 只画坐标轴和刻度，不再画完整矩形边框。
    draw_line_color(left, bottom, left, top, GRAY);
    draw_line_color(left, mid, right, mid, GRAY);

    // Y 轴箭头。
    draw_line_color(left, top, (uint16_t)(left - 3U), (uint16_t)(top + 5U), GRAY);
    draw_line_color(left, top, (uint16_t)(left + 3U), (uint16_t)(top + 5U), GRAY);

    // X 轴箭头。
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

/**
 * @brief 渲染主菜单页面。
 */
static void render_main_menu(const app_ui_state_t *ui)
{
    show_text_line(10U, 20U, "Main Menu", CYAN);
    show_menu_item(ui->cursor == 0U, 70U, "Square Wave");
    show_menu_item(ui->cursor == 1U, 102U, "Freq Measure");
    show_menu_item(ui->cursor == 2U, 134U, "DA Wave");
}

/**
 * @brief 渲染 PWM 频率设置页面。
 */
static void render_pwm_page(const app_monitor_state_t *state, const app_ui_state_t *ui)
{
    // 当前页面待显示的一行格式化文本
    char line[64];

    show_text_line(10U, 20U, "Square Wave", CYAN);

    // 编辑中显示 UI 草稿值；未编辑时显示已经写入 TIM1 的值。
    // 用户按上下键先看预览，按确认后才改定时器。
    snprintf(line, sizeof(line), "%c PWM Freq: %lu Hz %s",
             ui->cursor == 0U ? '>' : ' ',
             (unsigned long)(ui->editing != 0U ? ui->pwm_edit_frequency_hz : state->pwm_output.frequency_hz),
             ui->editing != 0U ? "[edit]" : "");
    show_text_line(18U, 70U, line, ui->cursor == 0U ? YELLOW : WHITE);

    show_menu_item(ui->cursor == 1U, 102U, "Back Home");
}

/**
 * @brief 渲染频率测量和串口上报设置页面。
 */
static void render_measure_page(const app_monitor_state_t *state, const app_ui_state_t *ui)
{
    // 当前页面待显示的一行格式化文本
    char line[64];

    show_text_line(10U, 20U, "Freq Measure", CYAN);

    snprintf(line, sizeof(line), "%c Serial Report: %s",
             ui->cursor == 0U ? '>' : ' ',
             ui->serial_auto_report_enabled != 0U ? "ON" : "OFF");
    show_text_line(18U, 64U, line, ui->cursor == 0U ? YELLOW : WHITE);

    show_menu_item(ui->cursor == 1U, 96U, "Back Home");

    // 频率以 Hz * 100 保存，这里拆成整数和小数两部分显示。
    snprintf(line, sizeof(line), "PA1 Measure: %lu.%02lu Hz",
             (unsigned long)(state->mains_frequency.frequency_x100 / 100U),
             (unsigned long)(state->mains_frequency.frequency_x100 % 100U));
    show_text_line(10U, 144U, line, GREEN);
}

/**
 * @brief 渲染 DAC 波形监看页面。
 */
static void render_da_page(const app_monitor_state_t *state, const app_ui_state_t *ui)
{
    // 当前页面待显示的一行格式化文本
    char line[64];
    // DA 波形图左边界
    uint16_t graph_left = DISPLAY_DA_GRAPH_X;
    // DA 波形图上边界
    uint16_t graph_top = DISPLAY_DA_GRAPH_Y;
    // DA 波形图宽度
    uint16_t graph_width = (uint16_t)(lcddev.width - DISPLAY_DA_GRAPH_X - 8U);
    // DA 波形图高度
    uint16_t graph_height = DISPLAY_DA_GRAPH_HEIGHT;
    // DA 波形图右边界
    uint16_t graph_right = (uint16_t)(graph_left + graph_width - 1U);
    // DA 波形图下边界
    uint16_t graph_bottom = (uint16_t)(graph_top + graph_height - 1U);
    // DA 波形图中线 y 坐标
    uint16_t graph_mid = (uint16_t)(graph_top + (graph_height / 2U));
    // 当前可绘制的波形样点数
    uint16_t count = state->dac_output.waveform_sample_count;

    if (count > APP_DAC_WAVEFORM_SAMPLES) {
        count = APP_DAC_WAVEFORM_SAMPLES;
    }

    show_text_line(10U, 20U, "DA Wave", CYAN);

    snprintf(line, sizeof(line), "Mode:%s Freq:%luHz",
             state->dac_output.config.mode == APP_DAC_MODE_DUAL ? "DUAL" : "SINGLE",
             (unsigned long)state->dac_output.config.frequency_hz);
    show_text_line(10U, 52U, line, WHITE);

    snprintf(line, sizeof(line), "Amp:%u Phase:%u",
             state->dac_output.config.amplitude,
             state->dac_output.config.phase_degrees);
    show_text_line(10U, 76U, line, WHITE);

    show_text_line(10U, 100U, "CH1 Green  CH2 Yellow", GRAY);
    show_menu_item(ui->cursor == 0U, 268U, "Back Home");

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

/**
 * @brief 根据当前页面刷新 LCD 内容。
 */
static void refresh_values(const app_monitor_state_t *state, const app_ui_state_t *ui)
{
    // 换页面时先清屏，否则上一页较长的字符串可能残留在新页面里。
    if (s_has_rendered_page == 0U || s_rendered_page != ui->page) {
        clear_content_area();
        s_rendered_page = ui->page;
        s_has_rendered_page = 1U;
    }

    if (ui->page == APP_UI_PAGE_PWM) {
        render_pwm_page(state, ui);
    } else if (ui->page == APP_UI_PAGE_MEASURE) {
        render_measure_page(state, ui);
    } else if (ui->page == APP_UI_PAGE_DA) {
        render_da_page(state, ui);
    } else {
        render_main_menu(ui);
    }
}

/**
 * @brief 初始化 LCD 并显示固定提示内容。
 */
void app_display_init(void)
{
    LCD_Init();

    // 上电后用红、绿、蓝快速闪一下屏幕，便于确认 LCD 初始化和背光正常。
    LCD_Clear(RED);
    delay_ms(120U);
    LCD_Clear(GREEN);
    delay_ms(120U);
    LCD_Clear(BLUE);
    delay_ms(120U);
    LCD_Clear(BLACK);

    POINT_COLOR = WHITE;
    BACK_COLOR = BLACK;
    show_text_line(10U, 300U, "USART1: 115200 8N1", GRAY);
    s_refresh_requested = 1U;
    s_has_rendered_page = 0U;
    s_last_refresh_us = 0U;
}

/**
 * @brief 按刷新请求或固定周期更新 LCD 显示。
 */
void app_display_task(const app_monitor_state_t *state, const app_ui_state_t *ui)
{
    if (state == 0 || ui == 0) {
        return;
    }

    //  以下两种情况出触发重绘：
    //  1. 有刷新请求；2. 到了固定刷新周期。
    if (s_refresh_requested != 0U || (state->now_us - s_last_refresh_us) >= DISPLAY_REFRESH_US) {
        refresh_values(state, ui);
        s_refresh_requested = 0U;
        s_last_refresh_us = state->now_us;
    }
}

/**
 * @brief 请求下一次显示任务重绘页面。
 */
void app_display_request_refresh(void)
{
    // 标识需要刷新
    s_refresh_requested = 1U;
}
