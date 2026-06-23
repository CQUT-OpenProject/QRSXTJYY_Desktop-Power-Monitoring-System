#include "app_ui.h"

#include "app_command.h"
#include "app_config.h"
#include "app_pwm.h"
#include "app_relay.h"

#define APP_UI_PWM_STEP_HZ 1U
#define APP_UI_DASHBOARD_ITEMS 1U
#define APP_UI_TEST_MENU_ITEMS 5U
#define APP_UI_PWM_ITEMS 2U
#define APP_UI_MEASURE_ITEMS 2U
#define APP_UI_DA_ITEMS 1U
#define APP_UI_INFO_ITEMS 1U

static app_ui_state_t s_state;

/** 获取当前页面可选项目数，用于光标环绕。 */
static uint8_t item_count_for_page(app_ui_page_t page)
{
    if (page == APP_UI_PAGE_DASHBOARD) {
        return APP_UI_DASHBOARD_ITEMS;
    }
    if (page == APP_UI_PAGE_TEST_MENU) {
        return APP_UI_TEST_MENU_ITEMS;
    }
    if (page == APP_UI_PAGE_PWM) {
        return APP_UI_PWM_ITEMS;
    }
    if (page == APP_UI_PAGE_MEASURE) {
        return APP_UI_MEASURE_ITEMS;
    }
    if (page == APP_UI_PAGE_DA) {
        return APP_UI_DA_ITEMS;
    }
    if (page == APP_UI_PAGE_INFO) {
        return APP_UI_INFO_ITEMS;
    }
    return 1U;
}

/**
 * @brief 根据页面状态同步继电器：Dashboard 吸合，其余释放。
 *
 * 只在页面发生实际切换时调用，不要在每帧 LCD 刷新中调用。
 */
static void ui_apply_relay_for_page(app_ui_page_t page)
{
    if (page == APP_UI_PAGE_DASHBOARD) {
        app_relay_set(1U);
    } else {
        app_relay_set(0U);
    }
}

/** 按方向移动光标，支持环绕。 */
static void move_cursor(int8_t direction)
{
    uint8_t count = item_count_for_page(s_state.page);

    if (direction < 0) {
        s_state.cursor = s_state.cursor == 0U ? (uint8_t)(count - 1U) : (uint8_t)(s_state.cursor - 1U);
    } else {
        s_state.cursor = (uint8_t)((s_state.cursor + 1U) % count);
    }
}

/** 编辑模式下调整 PWM 草稿值。 */
static void adjust_pwm(int8_t direction)
{
    if (direction > 0) {
        if (s_state.pwm_edit_frequency_hz < APP_PWM_MAX_HZ) {
            s_state.pwm_edit_frequency_hz += APP_UI_PWM_STEP_HZ;
        }
    } else if (s_state.pwm_edit_frequency_hz > APP_PWM_MIN_HZ) {
        s_state.pwm_edit_frequency_hz -= APP_UI_PWM_STEP_HZ;
    }
}

/** 进入或退出 PWM 编辑：进入时复制真实值到草稿，退出时写入模块。 */
static void toggle_pwm_edit(void)
{
    if (s_state.editing == 0U) {
        s_state.pwm_edit_frequency_hz = app_pwm_get_frequency();
        s_state.editing = 1U;
    } else {
        s_state.pwm_edit_frequency_hz = app_pwm_set_frequency(s_state.pwm_edit_frequency_hz);
        s_state.editing = 0U;
    }
}



/** Test Menu确认：0→PWM, 1→DA, 2→MEASURE, 3→INFO, 4→DASHBOARD。 */
static void handle_test_menu_confirm(void)
{
    if (s_state.cursor == 0U) {
        s_state.page = APP_UI_PAGE_PWM;
    } else if (s_state.cursor == 1U) {
        s_state.page = APP_UI_PAGE_DA;
    } else if (s_state.cursor == 2U) {
        s_state.page = APP_UI_PAGE_MEASURE;
    } else if (s_state.cursor == 3U) {
        s_state.page = APP_UI_PAGE_INFO;
    } else {
        s_state.page = APP_UI_PAGE_DASHBOARD;
    }
    s_state.cursor = 0U;
    s_state.editing = 0U;
    ui_apply_relay_for_page(s_state.page);
}

/** 功能页面确认：PWM/MEASURE 第 0 项操作，第 1 项返回；DA/INFO/DASHBOARD 直接返回。 */
static void handle_page_confirm(void)
{
    if (s_state.page == APP_UI_PAGE_DASHBOARD) {
        s_state.page = APP_UI_PAGE_TEST_MENU;
        s_state.cursor = 0U;
    } else if (s_state.page == APP_UI_PAGE_PWM) {
        if (s_state.cursor == 0U) {
            toggle_pwm_edit();
        } else {
            s_state.page = APP_UI_PAGE_TEST_MENU;
            s_state.cursor = 0U;
        }
    } else if (s_state.page == APP_UI_PAGE_MEASURE) {
        if (s_state.cursor == 0U) {
            /* LCD 菜单和串口命令共用 app_command 自动上报开关 */
            s_state.serial_auto_report_enabled =
                (uint8_t)(s_state.serial_auto_report_enabled == 0U ? 1U : 0U);
            app_command_set_auto_report_enabled(s_state.serial_auto_report_enabled);
        } else {
            s_state.page = APP_UI_PAGE_TEST_MENU;
            s_state.cursor = 0U;
        }
    } else if (s_state.page == APP_UI_PAGE_DA) {
        s_state.page = APP_UI_PAGE_TEST_MENU;
        s_state.cursor = 0U;
    } else if (s_state.page == APP_UI_PAGE_INFO) {
        s_state.page = APP_UI_PAGE_TEST_MENU;
        s_state.cursor = 0U;
    }
    ui_apply_relay_for_page(s_state.page);
}

void app_ui_init(void)
{
    s_state.page = APP_UI_PAGE_DASHBOARD;
    s_state.cursor = 0U;
    s_state.editing = 0U;
    s_state.serial_auto_report_enabled = app_command_get_auto_report_enabled();
    s_state.pwm_edit_frequency_hz = app_pwm_get_frequency();
    /* UI 初始化完成后按初始页面（Dashboard）同步继电器状态 */
    ui_apply_relay_for_page(s_state.page);
}

/**
 * @brief 按键事件输入菜单状态机。
 *
 * 编辑模式下上下键改数值草稿，确认键写入模块。
 * 正常模式下上下键移光标，确认键执行。
 */
uint8_t app_ui_handle_key(app_key_event_t key)
{
    if (key == APP_KEY_EVENT_NONE) {
        return 0U;
    }

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

    if (key == APP_KEY_EVENT_UP) {
        move_cursor(-1);
    } else if (key == APP_KEY_EVENT_DOWN) {
        move_cursor(1);
    } else if (key == APP_KEY_EVENT_CONFIRM) {
        if (s_state.page == APP_UI_PAGE_TEST_MENU) {
            handle_test_menu_confirm();
        } else {
            handle_page_confirm();
        }
    }

    return 1U;
}

const app_ui_state_t *app_ui_get_state(void)
{
    /* 返回前同步自动上报开关，串口命令 REPORT ON/OFF 后 LCD 立即显示新值 */
    s_state.serial_auto_report_enabled = app_command_get_auto_report_enabled();
    return &s_state;
}
