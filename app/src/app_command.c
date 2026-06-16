#include "app_command.h"

#include "app_dac.h"
#include "app_monitor_state.h"
#include "app_pwm.h"

#include <stdarg.h>
#include <stdio.h>

// LCD 菜单和串口命令都会改自动上报开关，所以这里只保存一份。
static uint8_t s_auto_report_enabled = 1U;

/**
 * @brief 将 ASCII 小写字母转换为大写。
 */
static char upper_char(char c)
{
    // 命令只用 ASCII 字符，手写大小写转换就够了。
    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }
    return c;
}

/**
 * @brief 判断字符是否为 ASCII 空白字符。
 */
static uint8_t is_ascii_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

/**
 * @brief 删除命令字符串尾部的空白字符。
 */
static void trim_trailing_space(char *text)
{
    // 当前扫描到的字符串长度
    uint8_t len = 0U;

    // 先找字符串末尾，再删掉尾部空白。长度上限可以防止坏输入一路读出去。
    while (text[len] != '\0' && len < (APP_COMMAND_INPUT_MAX - 1U)) {
        len++;
    }

    while (len > 0U && is_ascii_space(text[len - 1U]) != 0U) {
        text[--len] = '\0';
    }
}

/**
 * @brief 归一化命令行文本，去掉尾部空白和可见转义换行。
 */
static void normalize_command_line(char *cmd)
{
    // 当前命令字符串长度
    uint8_t len;

    // 有些串口工具会把回车换行发成可见的 "\r"、"\n" 字符。
    // 收到这种尾巴时也删掉，串口调试会省事一些。
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

/**
 * @brief 不区分大小写判断字符串是否以指定前缀开头。
 */
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

/**
 * @brief 不区分大小写判断两个字符串是否完全相等。
 */
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
 * @brief 从命令参数文本中解析无符号十进制整数。
 */
static uint8_t parse_uint(const char *text, uint32_t *value)
{
    // 逐位累加得到的整数结果
    uint32_t result = 0U;
    // 是否至少解析到一个数字
    uint8_t digits = 0U;

    // 命令数字前后允许有空格，例如 "PWM SET   50"。
    while (is_ascii_space(*text) != 0U) {
        text++;
    }

    // 逐字符把十进制文本转成整数。当前命令没有负数和小数，
    // 遇到这类字符就报 BAD_VALUE。
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

/**
 * @brief 清空命令处理结果结构体。
 */
static void clear_result(app_command_result_t *result)
{
    // 响应数组清空下标
    uint8_t i;

    // 每次处理命令前先清空 result，免得上一条命令的响应残留下来。
    result->monitor_changed = 0U;
    result->response_count = 0U;
    for (i = 0U; i < APP_COMMAND_RESPONSE_MAX; i++) {
        result->responses[i][0] = '\0';
    }
}

/**
 * @brief 向命令处理结果中追加一行格式化响应。
 */
static void add_response(app_command_result_t *result, const char *format, ...)
{
    // 传给 vsnprintf 的可变参数列表
    va_list args;

    // HELP 会返回多行。响应数组满了就不再写，避免越界。
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

/**
 * @brief 追加 HELP 命令的多行帮助响应。
 */
static void add_help(app_command_result_t *result)
{
    add_response(result, "OK CMD HELP, STATUS?, REPORT?");
    add_response(result, "OK CMD REPORT ON|OFF, PWM SET <hz>");
    add_response(result, "OK CMD DAC SET MODE SINGLE|DUAL, DAC SET FREQ <hz>");
    add_response(result, "OK CMD DAC SET AMP <code>, DAC SET PHASE <deg>");
}

/**
 * @brief 读取当前状态并追加 STATUS 响应。
 */
static void add_status(app_command_result_t *result)
{
    // 当前系统状态快照，用于格式化 STATUS 响应
    app_monitor_state_t state;

    // STATUS? 读取同一份监控快照，LCD 和串口看到的数据就能对上。
    app_monitor_state_read(&state);
    app_command_format_status(&state,
                              result->responses[result->response_count],
                              APP_COMMAND_RESPONSE_LINE_MAX);
    result->responses[result->response_count][APP_COMMAND_RESPONSE_LINE_MAX - 1U] = '\0';
    result->response_count++;
}

/**
 * @brief 处理 PWM SET 命令并更新 PWM 输出频率。
 */
static void handle_pwm_set(const char *cmd, app_command_result_t *result)
{
    // PWM SET 参数解析后的频率值
    uint32_t value;

    // PWM SET 后面必须是一个完整整数，解析失败就返回 BAD_VALUE。
    if (parse_uint(cmd + (sizeof("PWM SET ") - 1U), &value) == 0U) {
        add_response(result, "ERR BAD_VALUE");
        return;
    }

    value = app_pwm_set_frequency(value);
    result->monitor_changed = 1U;
    add_response(result, "OK PWM FREQ=%luHz", (unsigned long)value);
}

/**
 * @brief 处理 DAC SET MODE 命令并更新 DAC 输出模式。
 */
static void handle_dac_mode(const char *mode, app_command_result_t *result)
{
    // 当前 DAC 配置副本，修改模式后整份写回
    app_dac_config_t config;

    // 改 DAC 的一个字段时，先取出当前配置，再改目标字段，最后整份写回。
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
 * @brief 处理 DAC 数值类设置命令。
 */
static void handle_dac_number(const char *value_text,
                              app_command_result_t *result,
                              uint8_t field)
{
    // 当前 DAC 配置副本，修改单个字段后整份写回
    app_dac_config_t config;
    // DAC 数值命令解析后的无符号整数
    uint32_t value;

    if (parse_uint(value_text, &value) == 0U) {
        add_response(result, "ERR BAD_VALUE");
        return;
    }

    app_dac_get_config(&config);
    result->monitor_changed = 1U;
    if (field == 0U) {
        // field=0 表示修改频率。超范围值会在 app_dac_apply_config 里夹住。
        config.frequency_hz = value;
        app_dac_apply_config(&config);
        app_dac_get_config(&config);
        add_response(result, "OK DAC FREQ=%luHz", (unsigned long)config.frequency_hz);
    } else if (field == 1U) {
        // field=1 表示修改幅度。过大的幅度会被限制到安全码值。
        config.amplitude = (uint16_t)value;
        app_dac_apply_config(&config);
        app_dac_get_config(&config);
        add_response(result, "OK DAC AMP=%u", config.amplitude);
    } else {
        // 只剩 DAC SET PHASE 会走到这里，单位是度。
        config.phase_degrees = (uint16_t)value;
        app_dac_apply_config(&config);
        app_dac_get_config(&config);
        add_response(result, "OK DAC PHASE=%u", config.phase_degrees);
    }
}

/**
 * @brief 初始化命令模块默认状态。
 */
void app_command_init(void)
{
    s_auto_report_enabled = 1U;
}

/**
 * @brief 解析并执行一行文本命令。
 */
void app_command_handle_line(const char *line, app_command_result_t *result)
{
    // 命令文本的本地可修改副本
    char cmd[APP_COMMAND_INPUT_MAX];
    // 输入字符串复制下标
    uint8_t i;

    // result 是输出参数，不能为空。line 为空时返回 BAD_COMMAND。
    if (result == 0) {
        return;
    }

    clear_result(result);

    if (line == 0) {
        add_response(result, "ERR BAD_COMMAND");
        return;
    }

    // 先把输入复制到局部数组，再做裁剪和大小写匹配。
    // normalize_command_line 后面只改这份副本，不碰中断缓冲区或只读字符串。
    for (i = 0U; i < (APP_COMMAND_INPUT_MAX - 1U) && line[i] != '\0'; i++) {
        cmd[i] = line[i];
    }
    cmd[i] = '\0';
    normalize_command_line(cmd);

    // 先匹配完整命令，再匹配带参数的命令前缀。
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
    } else {
        add_response(result, "ERR BAD_COMMAND");
    }
}

/**
 * @brief 设置串口自动上报开关。
 */
void app_command_set_auto_report_enabled(uint8_t enabled)
{
    s_auto_report_enabled = enabled == 0U ? 0U : 1U;
}

/**
 * @brief 获取串口自动上报开关状态。
 */
uint8_t app_command_get_auto_report_enabled(void)
{
    return s_auto_report_enabled;
}

/**
 * @brief 将监控状态快照格式化为串口 STATUS 响应文本。
 */
void app_command_format_status(const app_monitor_state_t *state,
                               char *line,
                               uint16_t line_size)
{
    // STATUS 行比较长，用 snprintf 写，避免超过调用者给的缓冲区。
    if (line == 0 || line_size == 0U) {
        return;
    }

    if (state == 0) {
        snprintf(line, line_size, "ERR BAD_VALUE");
        return;
    }

    snprintf(line,
             line_size,
             "OK STATUS MEAS=%lu.%02luHz PWM=%luHz REPORT=%s DAC_MODE=%s DAC_FREQ=%luHz DAC_AMP=%u DAC_PHASE=%u",
             (unsigned long)(state->mains_frequency.frequency_x100 / 100U),
             (unsigned long)(state->mains_frequency.frequency_x100 % 100U),
             (unsigned long)state->pwm_output.frequency_hz,
             s_auto_report_enabled != 0U ? "ON" : "OFF",
             state->dac_output.config.mode == APP_DAC_MODE_DUAL ? "DUAL" : "SINGLE",
             (unsigned long)state->dac_output.config.frequency_hz,
             state->dac_output.config.amplitude,
             state->dac_output.config.phase_degrees);
    line[line_size - 1U] = '\0';
}
