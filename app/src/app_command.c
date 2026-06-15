#include "app_command.h"

#include "app_dac.h"
#include "app_monitor_state.h"
#include "app_pwm.h"

#include <stdarg.h>
#include <stdio.h>

#define APP_COMMAND_INPUT_MAX 64U

static uint8_t s_auto_report_enabled = 1U;

static char upper_char(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }
    return c;
}

static uint8_t is_ascii_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

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

static void clear_result(app_command_result_t *result)
{
    uint8_t i;

    result->monitor_changed = 0U;
    result->response_count = 0U;
    for (i = 0U; i < APP_COMMAND_RESPONSE_MAX; i++) {
        result->responses[i][0] = '\0';
    }
}

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

static void add_help(app_command_result_t *result)
{
    add_response(result, "OK CMD HELP, STATUS?, REPORT?");
    add_response(result, "OK CMD REPORT ON|OFF, PWM SET <hz>");
    add_response(result, "OK CMD DAC SET MODE SINGLE|DUAL, DAC SET FREQ <hz>");
    add_response(result, "OK CMD DAC SET AMP <code>, DAC SET PHASE <deg>");
}

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

static void handle_dac_mode(const char *mode, app_command_result_t *result)
{
    if (equals_ci(mode, "SINGLE") != 0U) {
        app_dac_set_mode(APP_DAC_MODE_SINGLE);
        result->monitor_changed = 1U;
        add_response(result, "OK DAC MODE=SINGLE");
    } else if (equals_ci(mode, "DUAL") != 0U) {
        app_dac_set_mode(APP_DAC_MODE_DUAL);
        result->monitor_changed = 1U;
        add_response(result, "OK DAC MODE=DUAL");
    } else {
        add_response(result, "ERR BAD_VALUE");
    }
}

static void handle_dac_number(const char *value_text,
                              app_command_result_t *result,
                              uint8_t field)
{
    uint32_t value;

    if (parse_uint(value_text, &value) == 0U) {
        add_response(result, "ERR BAD_VALUE");
        return;
    }

    result->monitor_changed = 1U;
    if (field == 0U) {
        app_dac_set_frequency(value);
        add_response(result, "OK DAC FREQ=%luHz", (unsigned long)app_dac_get_frequency());
    } else if (field == 1U) {
        app_dac_set_amplitude((uint16_t)value);
        add_response(result, "OK DAC AMP=%u", app_dac_get_amplitude());
    } else {
        app_dac_set_phase((uint16_t)value);
        add_response(result, "OK DAC PHASE=%u", app_dac_get_phase());
    }
}

void app_command_init(void)
{
    s_auto_report_enabled = 1U;
}

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

    snprintf(line,
             line_size,
             "OK STATUS MEAS=%lu.%02luHz PWM=%luHz REPORT=%s DAC_MODE=%s DAC_FREQ=%luHz DAC_AMP=%u DAC_PHASE=%u",
             (unsigned long)(state->mains_frequency.frequency_x100 / 100U),
             (unsigned long)(state->mains_frequency.frequency_x100 % 100U),
             (unsigned long)state->pwm_output.frequency_hz,
             s_auto_report_enabled != 0U ? "ON" : "OFF",
             state->dac_output.dual_output_enabled != 0U ? "DUAL" : "SINGLE",
             (unsigned long)state->dac_output.frequency_hz,
             state->dac_output.amplitude,
             state->dac_output.phase_degrees);
    line[line_size - 1U] = '\0';
}
