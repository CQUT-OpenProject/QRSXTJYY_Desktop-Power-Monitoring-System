#include "app_display.h"

#include "delay.h"
#include "lcd.h"

#include <stdio.h>

#define DISPLAY_REFRESH_US 500000U
#define DISPLAY_CONTENT_BOTTOM 296U

/* 主循环或 UI 改变状态时置 1，下一次 display_task 会重画。 */
static uint8_t s_refresh_requested = 1U;

/* 记住上一次画过的页面，换页时要先清内容区。 */
static app_ui_page_t s_rendered_page = APP_UI_PAGE_MENU;
static uint8_t s_has_rendered_page;

/* 上次刷新时间，防止 LCD 刷新太频繁造成闪烁。 */
static uint32_t s_last_refresh_us;

static void show_text_line(uint16_t x, uint16_t y, const char *text, uint16_t color)
{
    /*
     * ALIENTEK LCD 库用全局 POINT_COLOR/BACK_COLOR 控制文字颜色。
     * 每次显示一行前先清掉这一行背景，避免旧字符残留。
     */
    POINT_COLOR = color;
    BACK_COLOR = BLACK;
    LCD_Fill(x, y, lcddev.width - 1U, (uint16_t)(y + 15U), BLACK);
    LCD_ShowString(x, y, (uint16_t)(lcddev.width - x), 16U, 16U, (u8 *)text);
}

static void clear_content_area(void)
{
    /* 只清内容区，底部串口提示栏保留不动。 */
    LCD_Fill(0U, 0U, lcddev.width - 1U, DISPLAY_CONTENT_BOTTOM, BLACK);
}

static void show_menu_item(uint8_t selected, uint16_t y, const char *text)
{
    char line[48];

    /* 用 '>' 标出当前光标位置，LCD 上也很直观。 */
    snprintf(line, sizeof(line), "%c %s", selected != 0U ? '>' : ' ', text);
    show_text_line(18U, y, line, selected != 0U ? YELLOW : WHITE);
}

static void render_main_menu(const app_ui_state_t *ui)
{
    show_text_line(10U, 20U, "Main Menu", CYAN);
    show_menu_item(ui->cursor == 0U, 70U, "Square Wave");
    show_menu_item(ui->cursor == 1U, 102U, "Freq Measure");
}

static void render_pwm_page(const app_monitor_state_t *state, const app_ui_state_t *ui)
{
    char line[64];

    show_text_line(10U, 20U, "Square Wave", CYAN);

    /*
     * 编辑中显示 UI 草稿值；未编辑时显示已经写入 TIM1 的值。
     * 用户按上下键先看预览，按确认后才改定时器。
     */
    snprintf(line, sizeof(line), "%c PWM Freq: %lu Hz %s",
             ui->cursor == 0U ? '>' : ' ',
             (unsigned long)(ui->editing != 0U ? ui->pwm_edit_frequency_hz : state->pwm_output.frequency_hz),
             ui->editing != 0U ? "[edit]" : "");
    show_text_line(18U, 70U, line, ui->cursor == 0U ? YELLOW : WHITE);

    show_menu_item(ui->cursor == 1U, 102U, "Back Home");
}

static void render_measure_page(const app_monitor_state_t *state, const app_ui_state_t *ui)
{
    char line[64];

    show_text_line(10U, 20U, "Freq Measure", CYAN);

    snprintf(line, sizeof(line), "%c Serial Report: %s",
             ui->cursor == 0U ? '>' : ' ',
             ui->serial_auto_report_enabled != 0U ? "ON" : "OFF");
    show_text_line(18U, 64U, line, ui->cursor == 0U ? YELLOW : WHITE);

    show_menu_item(ui->cursor == 1U, 96U, "Back Home");

    /*
     * 频率以 Hz * 100 保存，这里拆成整数和小数两部分显示。
     */
    snprintf(line, sizeof(line), "PA1 Measure: %lu.%02lu Hz",
             (unsigned long)(state->mains_frequency.frequency_x100 / 100U),
             (unsigned long)(state->mains_frequency.frequency_x100 % 100U));
    show_text_line(10U, 144U, line, GREEN);
}

static void refresh_values(const app_monitor_state_t *state, const app_ui_state_t *ui)
{
    /*
     * 换页面时先清屏，否则上一页较长的字符串可能残留在新页面里。
     */
    if (s_has_rendered_page == 0U || s_rendered_page != ui->page) {
        clear_content_area();
        s_rendered_page = ui->page;
        s_has_rendered_page = 1U;
    }

    if (ui->page == APP_UI_PAGE_PWM) {
        render_pwm_page(state, ui);
    } else if (ui->page == APP_UI_PAGE_MEASURE) {
        render_measure_page(state, ui);
    } else {
        render_main_menu(ui);
    }
}

void app_display_init(void)
{
    LCD_Init();

    /*
     * 上电后用红、绿、蓝快速闪一下屏幕，便于确认 LCD 初始化和背光正常。
     */
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

void app_display_task(const app_monitor_state_t *state, const app_ui_state_t *ui)
{
    if (state == 0 || ui == 0) {
        return;
    }

    /*
     * 两种情况会重画：有刷新请求，或者到了固定刷新周期。
     * 固定周期用来更新测频值，用户不按键也能看到新数据。
     */
    if (s_refresh_requested != 0U || (state->now_us - s_last_refresh_us) >= DISPLAY_REFRESH_US) {
        refresh_values(state, ui);
        s_refresh_requested = 0U;
        s_last_refresh_us = state->now_us;
    }
}

void app_display_request_refresh(void)
{
    /* 这里只置标志，不直接画屏；画屏留给 app_display_task。 */
    s_refresh_requested = 1U;
}
