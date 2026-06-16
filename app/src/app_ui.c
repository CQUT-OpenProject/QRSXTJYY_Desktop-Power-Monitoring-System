#include "app_ui.h"

#include "app_command.h"
#include "app_pwm.h"

#define APP_UI_PWM_STEP_HZ 1U
#define APP_UI_PWM_MIN_HZ 1U
#define APP_UI_PWM_MAX_HZ 100000U
#define APP_UI_MENU_ITEMS 3U
#define APP_UI_PWM_ITEMS 2U
#define APP_UI_MEASURE_ITEMS 2U
#define APP_UI_DA_ITEMS 1U

// 菜单状态只有这一份。按键事件改它，显示代码读它。
static app_ui_state_t s_state;

/**
 * @brief 获取指定 UI 页面可选择的项目数量。
 */
static uint8_t item_count_for_page(app_ui_page_t page)
{
    // 不同页面项目数不同，光标环绕时要知道当前页有几项。
    if (page == APP_UI_PAGE_PWM) {
        return APP_UI_PWM_ITEMS;
    }
    if (page == APP_UI_PAGE_MEASURE) {
        return APP_UI_MEASURE_ITEMS;
    }
    if (page == APP_UI_PAGE_DA) {
        return APP_UI_DA_ITEMS;
    }
    return APP_UI_MENU_ITEMS;
}

/**
 * @brief 按方向移动当前页面光标。
 */
static void move_cursor(int8_t direction)
{
    // 当前页面可选择的项目数量
    uint8_t count = item_count_for_page(s_state.page);

    // 光标上下移动采用环绕方式：第一项再向上会跳到最后一项。
    if (direction < 0) {
        s_state.cursor = s_state.cursor == 0U ? (uint8_t)(count - 1U) : (uint8_t)(s_state.cursor - 1U);
    } else {
        s_state.cursor = (uint8_t)((s_state.cursor + 1U) % count);
    }
}

/**
 * @brief 在编辑模式下调整 PWM 频率草稿值。
 */
static void adjust_pwm(int8_t direction)
{
    // 编辑 PWM 频率时每次只改 1 Hz，演示时更容易观察。
    if (direction > 0) {
        if (s_state.pwm_edit_frequency_hz < APP_UI_PWM_MAX_HZ) {
            s_state.pwm_edit_frequency_hz += APP_UI_PWM_STEP_HZ;
        }
    } else if (s_state.pwm_edit_frequency_hz > APP_UI_PWM_MIN_HZ) {
        s_state.pwm_edit_frequency_hz -= APP_UI_PWM_STEP_HZ;
    }
}

/**
 * @brief 进入或确认退出 PWM 频率编辑模式。
 */
static void toggle_pwm_edit(void)
{
    if (s_state.editing == 0U) {
        // 进入编辑时先把真实 PWM 频率复制成草稿值。
        s_state.pwm_edit_frequency_hz = app_pwm_get_frequency();
        s_state.editing = 1U;
    } else {
        // 再次确认时，草稿值才写入 PWM 模块，PA8 输出随之改变。
        s_state.pwm_edit_frequency_hz = app_pwm_set_frequency(s_state.pwm_edit_frequency_hz);
        s_state.editing = 0U;
    }
}

/**
 * @brief 返回主菜单并清除编辑状态。
 */
static void enter_menu(void)
{
    // 回到主菜单时退出编辑状态，避免下次进入页面还停在编辑模式。
    s_state.page = APP_UI_PAGE_MENU;
    s_state.cursor = 0U;
    s_state.editing = 0U;
}

/**
 * @brief 处理主菜单页面的确认键。
 */
static void handle_menu_confirm(void)
{
    // 主菜单：第 0 项进入 PWM，第 1 项进入测频，第 2 项进入 DA 波形页。
    if (s_state.cursor == 0U) {
        s_state.page = APP_UI_PAGE_PWM;
    } else if (s_state.cursor == 1U) {
        s_state.page = APP_UI_PAGE_MEASURE;
    } else {
        s_state.page = APP_UI_PAGE_DA;
    }
    s_state.cursor = 0U;
    s_state.editing = 0U;
}

/**
 * @brief 处理功能页面的确认键。
 */
static void handle_page_confirm(void)
{
    // PWM 和测频页的第 0 项是主要操作，第 1 项是返回主页。
    // DA 页只负责监看波形，第 0 项直接返回主页。
    if (s_state.page == APP_UI_PAGE_PWM) {
        if (s_state.cursor == 0U) {
            toggle_pwm_edit();
        } else {
            enter_menu();
        }
    } else if (s_state.page == APP_UI_PAGE_MEASURE) {
        if (s_state.cursor == 0U) {
            // LCD 菜单和串口命令共用 app_command 中的自动上报开关。
            s_state.serial_auto_report_enabled =
                (uint8_t)(s_state.serial_auto_report_enabled == 0U ? 1U : 0U);
            app_command_set_auto_report_enabled(s_state.serial_auto_report_enabled);
        } else {
            enter_menu();
        }
    } else if (s_state.page == APP_UI_PAGE_DA) {
        enter_menu();
    }
}

/**
 * @brief 初始化 UI 状态机。
 */
void app_ui_init(void)
{
    // 默认停在主菜单，并读取当前已经生效的状态作为初始显示值。
    s_state.page = APP_UI_PAGE_MENU;
    s_state.cursor = 0U;
    s_state.editing = 0U;
    s_state.serial_auto_report_enabled = app_command_get_auto_report_enabled();
    s_state.pwm_edit_frequency_hz = app_pwm_get_frequency();
}

/**
 * @brief 将按键事件送入 UI 状态机并返回界面是否变化。
 */
uint8_t app_ui_handle_key(app_key_event_t key)
{
    if (key == APP_KEY_EVENT_NONE) {
        return 0U;
    }

    // 正在编辑数值时，上下键不移动菜单，而是改变数值草稿。
    if (s_state.editing != 0U && s_state.cursor == 0U) {
        if (key == APP_KEY_EVENT_UP) {
            adjust_pwm(1);
        } else if (key == APP_KEY_EVENT_DOWN) {
            adjust_pwm(-1);
        } else if (key == APP_KEY_EVENT_CONFIRM) {
            toggle_pwm_edit();
        }
        return 1U;
    }

    // 非编辑状态下，上下键移动光标，确认键执行当前项。
    if (key == APP_KEY_EVENT_UP) {
        move_cursor(-1);
    } else if (key == APP_KEY_EVENT_DOWN) {
        move_cursor(1);
    } else if (key == APP_KEY_EVENT_CONFIRM) {
        if (s_state.page == APP_UI_PAGE_MENU) {
            handle_menu_confirm();
        } else {
            handle_page_confirm();
        }
    }

    return 1U;
}

/**
 * @brief 获取当前 UI 状态指针。
 */
const app_ui_state_t *app_ui_get_state(void)
{
    // 返回前同步一次自动上报开关。串口命令 REPORT ON/OFF 改状态后，
    // LCD 下次刷新就能显示新值。
    s_state.serial_auto_report_enabled = app_command_get_auto_report_enabled();
    return &s_state;
}
