#include "app_command.h"

#include "app_adc.h"
#include "app_dac.h"
#include "app_monitor_state.h"
#include "app_pwm.h"
#include "app_screenshot.h"

#include <stdarg.h>
#include <stdio.h>

/* LCD 菜单和串口命令都会改自动上报开关，所以这里只保存一份。 */
static uint8_t s_auto_report_enabled = 0U;

/** 将小写字母转换为大写。 */
static char upper_char(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }
    return c;
}

/** 判断字符是否为空白。 */
static uint8_t is_ascii_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

/** 删除字符串尾部空白。长度上限防止坏输入一路读出去。 */
static void trim_trailing_space(char *text)
{
    uint8_t len = 0U;

    while (text[len] != '\0' && len < (APP_COMMAND_INPUT_MAX - 1U)) {
        len++;
    }

    while (len > 0U && is_ascii_space(text[len - 1U]) != 0U) {
        text[--len] = '\0';
    }
}

/** 去掉文本尾部空白和转义换行 (\\r/\\n)。 */
static void normalize_command_line(char *cmd)
{
    uint8_t len;

    trim_trailing_space(cmd);
    while (1) {
        len = 0U;
        while (cmd[len] != '\0' && len < (APP_COMMAND_INPUT_MAX - 1U)) {
            len++;
        }

        if (len >= 2U && cmd[len - 2U] == '\\' &&
            (upper_char(cmd[len - 1U]) == 'R' || upper_char(cmd[len - 1U]) == 'N')) {
            cmd[len - 2U] = '\0';
            trim_trailing_space(cmd);
        } else {
            break;
        }
    }
}

/** 不区分大小写判断 prefix 是否为 text 的前缀。 */
static uint8_t starts_with_ci(const char *text, const char *prefix)
{
    while (*prefix != '\0') {
        if (upper_char(*text) != upper_char(*prefix)) {
            return 0U;
        }
        text++;
        prefix++;
    }
    return 1U;
}

/** 不区分大小写判断两个字符串是否相等。 */
static uint8_t equals_ci(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (upper_char(*a) != upper_char(*b)) {
            return 0U;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/**
 * @brief 从命令参数中解析无符号十进制整数。
 *
 * 数字前后允许空白；出现负号、小数点或非空白尾随字符时返回失败。
 */
static uint8_t parse_uint(const char *text, uint32_t *value)
{
    uint32_t result = 0U;
    uint8_t digits = 0U;

    while (is_ascii_space(*text) != 0U) {
        text++;
    }

    while (*text >= '0' && *text <= '9') {
        result = (result * 10U) + (uint32_t)(*text - '0');
        digits = 1U;
        text++;
    }

    while (is_ascii_space(*text) != 0U) {
        text++;
    }

    if (digits == 0U || *text != '\0') {
        return 0U;
    }

    *value = result;
    return 1U;
}

/** 清空命令处理结果结构体。 */
static void clear_result(app_command_result_t *result)
{
    uint8_t i;

    result->monitor_changed = 0U;
    result->response_count = 0U;
    for (i = 0U; i < APP_COMMAND_RESPONSE_MAX; i++) {
        result->responses[i][0] = '\0';
    }
}

/** 追加一行格式化响应文本。响应数组满了就不再写，避免越界。 */
static void add_response(app_command_result_t *result, const char *format, ...)
{
    va_list args;

    if (result->response_count >= APP_COMMAND_RESPONSE_MAX) {
        return;
    }

    va_start(args, format);
    vsnprintf(result->responses[result->response_count],
              APP_COMMAND_RESPONSE_LINE_MAX,
              format,
              args);
    va_end(args);
    result->responses[result->response_count][APP_COMMAND_RESPONSE_LINE_MAX - 1U] = '\0';
    result->response_count++;
}

/** 追加 HELP 命令的多行帮助响应。 */
static void add_help(app_command_result_t *result)
{
    add_response(result, "OK CMD HELP, STATUS?, REPORT?");
    add_response(result, "OK CMD REPORT ON|OFF, PWM SET <hz>");
    add_response(result, "OK CMD DAC SET MODE|FREQ|AMP|PHASE <val>");
    add_response(result, "OK CMD CAL ZERO");
}

/** 读取当前监控快照并追加 STATUS 响应行。 */
static void add_status(app_command_result_t *result)
{
    app_monitor_state_t state;

    app_monitor_state_read(&state);
    app_command_format_status(&state,
                              result->responses[result->response_count],
                              APP_COMMAND_RESPONSE_LINE_MAX);
    result->responses[result->response_count][APP_COMMAND_RESPONSE_LINE_MAX - 1U] = '\0';
    result->response_count++;
}

/** 处理 PWM SET 命令并更新 PWM 输出频率。 */
static void handle_pwm_set(const char *cmd, app_command_result_t *result)
{
    uint32_t value;

    if (parse_uint(cmd + (sizeof("PWM SET ") - 1U), &value) == 0U) {
        add_response(result, "ERR BAD_VALUE");
        return;
    }

    value = app_pwm_set_frequency(value);
    result->monitor_changed = 1U;
    add_response(result, "OK PWM FREQ=%luHz", (unsigned long)value);
}

/** 处理 DAC SET MODE 命令：SINGLE 或 DUAL。 */
static void handle_dac_mode(const char *mode, app_command_result_t *result)
{
    app_dac_config_t config;

    /* 先读当前配置，改一个字段后整份写回 */
    app_dac_get_config(&config);
    if (equals_ci(mode, "SINGLE") != 0U) {
        config.mode = APP_DAC_MODE_SINGLE;
        app_dac_apply_config(&config);
        result->monitor_changed = 1U;
        add_response(result, "OK DAC MODE=SINGLE");
    } else if (equals_ci(mode, "DUAL") != 0U) {
        config.mode = APP_DAC_MODE_DUAL;
        app_dac_apply_config(&config);
        result->monitor_changed = 1U;
        add_response(result, "OK DAC MODE=DUAL");
    } else {
        add_response(result, "ERR BAD_VALUE");
    }
}

/**
 * @brief 处理 DAC 数值类设置命令 (FREQ / AMP / PHASE)。
 *
 * @param field 0 = 频率, 1 = 幅度, 2 = 相位。
 */
static void handle_dac_number(const char *value_text,
                              app_command_result_t *result,
                              uint8_t field)
{
    app_dac_config_t config;
    uint32_t value;

    if (parse_uint(value_text, &value) == 0U) {
        add_response(result, "ERR BAD_VALUE");
        return;
    }

    app_dac_get_config(&config);
    result->monitor_changed = 1U;
    if (field == 0U) {
        /* 修改频率，超范围值在 app_dac_apply_config 中夹住 */
        config.frequency_hz = value;
        app_dac_apply_config(&config);
        app_dac_get_config(&config);
        add_response(result, "OK DAC FREQ=%luHz", (unsigned long)config.frequency_hz);
    } else if (field == 1U) {
        /* 修改幅度，过大值被限制到安全码值 */
        config.amplitude = (uint16_t)value;
        app_dac_apply_config(&config);
        app_dac_get_config(&config);
        add_response(result, "OK DAC AMP=%u", config.amplitude);
    } else {
        /* DAC SET PHASE，单位度 */
        config.phase_degrees = (uint16_t)value;
        app_dac_apply_config(&config);
        app_dac_get_config(&config);
        add_response(result, "OK DAC PHASE=%u", config.phase_degrees);
    }
}

/** 处理 CAL ZERO 命令：执行 ADC 零偏移校准。 */
static void handle_cal_zero(app_command_result_t *result)
{
    app_adc_calibrate_zero();
    result->monitor_changed = 1U;
    add_response(result, "OK CAL ZERO DONE");
}

void app_command_init(void)
{
    s_auto_report_enabled = 0U;
}

/**
 * @brief 解析并执行一行文本命令。
 *
 * 先复制输入到局部缓冲做裁剪和归一化，不修改 ISR 缓冲区。
 * 先匹配完整命令，再匹配带参数的命令前缀。
 */
void app_command_handle_line(const char *line, app_command_result_t *result)
{
    char cmd[APP_COMMAND_INPUT_MAX];
    uint8_t i;

    if (result == 0) {
        return;
    }

    clear_result(result);

    if (line == 0) {
        add_response(result, "ERR BAD_COMMAND");
        return;
    }

    for (i = 0U; i < (APP_COMMAND_INPUT_MAX - 1U) && line[i] != '\0'; i++) {
        cmd[i] = line[i];
    }
    cmd[i] = '\0';
    normalize_command_line(cmd);

    if (equals_ci(cmd, "HELP") != 0U) {
        add_help(result);
    } else if (equals_ci(cmd, "STATUS?") != 0U) {
        add_status(result);
    } else if (equals_ci(cmd, "REPORT?") != 0U) {
        add_response(result,
                     "OK REPORT %s",
                     s_auto_report_enabled != 0U ? "ON" : "OFF");
    } else if (equals_ci(cmd, "REPORT ON") != 0U) {
        app_command_set_auto_report_enabled(1U);
        result->monitor_changed = 1U;
        add_response(result, "OK REPORT ON");
    } else if (equals_ci(cmd, "REPORT OFF") != 0U) {
        app_command_set_auto_report_enabled(0U);
        result->monitor_changed = 1U;
        add_response(result, "OK REPORT OFF");
    } else if (starts_with_ci(cmd, "PWM SET ") != 0U) {
        handle_pwm_set(cmd, result);
    } else if (starts_with_ci(cmd, "DAC SET MODE ") != 0U) {
        handle_dac_mode(cmd + (sizeof("DAC SET MODE ") - 1U), result);
    } else if (starts_with_ci(cmd, "DAC SET FREQ ") != 0U) {
        handle_dac_number(cmd + (sizeof("DAC SET FREQ ") - 1U), result, 0U);
    } else if (starts_with_ci(cmd, "DAC SET AMP ") != 0U) {
        handle_dac_number(cmd + (sizeof("DAC SET AMP ") - 1U), result, 1U);
    } else if (starts_with_ci(cmd, "DAC SET PHASE ") != 0U) {
        handle_dac_number(cmd + (sizeof("DAC SET PHASE ") - 1U), result, 2U);
    } else if (equals_ci(cmd, "CAL ZERO") != 0U) {
        handle_cal_zero(result);
    } else if (equals_ci(cmd, "SHOT") != 0U) {
        app_screenshot_dump();
        add_response(result, "OK SHOT DONE");
    } else {
        add_response(result, "ERR BAD_COMMAND");
    }
}

void app_command_set_auto_report_enabled(uint8_t enabled)
{
    s_auto_report_enabled = enabled == 0U ? 0U : 1U;
}

uint8_t app_command_get_auto_report_enabled(void)
{
    return s_auto_report_enabled;
}

/**
 * @brief 将监控快照格式化为一行 STATUS 响应文本。
 */
void app_command_format_status(const app_monitor_state_t *state,
                               char *line,
                               uint16_t line_size)
{
    if (line == 0 || line_size == 0U) {
        return;
    }

    if (state == 0) {
        snprintf(line, line_size, "ERR BAD_VALUE");
        return;
    }

    if (state->adc.params != 0) {
        int32_t p_val = state->adc.params->active_power_x10;
        uint32_t p_abs = (p_val < 0) ? (uint32_t)(-p_val) : (uint32_t)p_val;
        const char *p_sign = (p_val < 0) ? "-" : "";
        snprintf(line,
                 line_size,
                 "OK STATUS MEAS=%lu.%02luHz PWM=%luHz REPORT=%s "
                 "DAC_MODE=%s DAC_FREQ=%luHz DAC_AMP=%u DAC_PHASE=%u "
                 "VRMS=%lu.%02lu IRMS=%lu.%03lu ILK=%lu.%03lu "
                 "P=%s%lu.%01lu S=%lu.%01lu PF=%lu.%03lu CAL=%s",
                 (unsigned long)(state->mains_frequency.frequency_x100 / 100U),
                 (unsigned long)(state->mains_frequency.frequency_x100 % 100U),
                 (unsigned long)state->pwm_output.frequency_hz,
                 s_auto_report_enabled != 0U ? "ON" : "OFF",
                 state->dac_output.config.mode == APP_DAC_MODE_DUAL ? "DUAL" : "SINGLE",
                 (unsigned long)state->dac_output.config.frequency_hz,
                 state->dac_output.config.amplitude,
                 state->dac_output.config.phase_degrees,
                 (unsigned long)(state->adc.params->vrms_x100 / 100U),
                 (unsigned long)(state->adc.params->vrms_x100 % 100U),
                 (unsigned long)(state->adc.params->irms_x1000 / 1000U),
                 (unsigned long)(state->adc.params->irms_x1000 % 1000U),
                 (unsigned long)(state->adc.params->ilk_rms_x1000 / 1000U),
                 (unsigned long)(state->adc.params->ilk_rms_x1000 % 1000U),
                 p_sign,
                 (unsigned long)(p_abs / 10U),
                 (unsigned long)(p_abs % 10U),
                 (unsigned long)(state->adc.params->apparent_power_x10 / 10U),
                 (unsigned long)(state->adc.params->apparent_power_x10 % 10U),
                 (unsigned long)(state->adc.params->power_factor_x1000 / 1000U),
                 (unsigned long)(state->adc.params->power_factor_x1000 % 1000U),
                 state->adc.params->zero_calibrated != 0U ? "YES" : "NO");
    } else {
        snprintf(line,
                 line_size,
                 "OK STATUS MEAS=%lu.%02luHz PWM=%luHz REPORT=%s "
                 "DAC_MODE=%s DAC_FREQ=%luHz DAC_AMP=%u DAC_PHASE=%u "
                 "VRMS=NA",
                 (unsigned long)(state->mains_frequency.frequency_x100 / 100U),
                 (unsigned long)(state->mains_frequency.frequency_x100 % 100U),
                 (unsigned long)state->pwm_output.frequency_hz,
                 s_auto_report_enabled != 0U ? "ON" : "OFF",
                 state->dac_output.config.mode == APP_DAC_MODE_DUAL ? "DUAL" : "SINGLE",
                 (unsigned long)state->dac_output.config.frequency_hz,
                 state->dac_output.config.amplitude,
                 state->dac_output.config.phase_degrees);
    }
    line[line_size - 1U] = '\0';
}
