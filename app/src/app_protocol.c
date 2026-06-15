#include "app_protocol.h"

#include "app_command.h"
#include "misc.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"

#define APP_PROTOCOL_BAUDRATE 115200U
#define APP_PROTOCOL_LINE_MAX 64U
#define APP_PROTOCOL_IDLE_TIMEOUT_US 300000U

static volatile char s_rx_line[APP_PROTOCOL_LINE_MAX];
static volatile uint8_t s_rx_len;
static volatile char s_pending_line[APP_PROTOCOL_LINE_MAX];
static volatile uint8_t s_pending_ready;

/*
 * observed_rx_len 和 rx_idle_start_us 处理“输入了命令但没按回车”的情况。
 * 如果串口缓冲区长度一段时间不变，就把当前内容当作一行命令。
 */
static uint8_t s_observed_rx_len;
static uint32_t s_rx_idle_start_us;

static void usart_send_char(char c)
{
    /* 等发送寄存器空了，再写入一个字节；这是最简单的阻塞发送。 */
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) {
    }
    USART_SendData(USART1, (uint16_t)c);
}

static void queue_rx_line(void)
{
    uint8_t i;

    /*
     * ISR 收到换行，或主循环判定输入停住后，把 RX 缓冲复制到 pending_line。
     * 命令解析留在主循环里做，中断里少干活。
     */
    s_rx_line[s_rx_len] = '\0';
    if (s_pending_ready == 0U) {
        for (i = 0U; i <= s_rx_len && i < APP_PROTOCOL_LINE_MAX; i++) {
            s_pending_line[i] = s_rx_line[i];
        }
        s_pending_line[APP_PROTOCOL_LINE_MAX - 1U] = '\0';
        s_pending_ready = 1U;
    }
    s_rx_len = 0U;
}

static uint8_t dispatch_pending_line(char *cmd)
{
    app_command_result_t result;
    uint8_t i;

    /*
     * 协议层不解析 PWM/DAC 命令，只把完整行交给 app_command，
     * 再逐行发送 app_command 给出的响应。
     */
    app_command_handle_line(cmd, &result);
    for (i = 0U; i < result.response_count; i++) {
        app_protocol_send_line(result.responses[i]);
    }
    return result.monitor_changed;
}

void app_protocol_init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef nvic;

    /* USART1 使用 PA9(TX) 和 PA10(RX)，配置 GPIO 前必须先打开端口时钟。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    GPIO_StructInit(&gpio);
    /* PA9 配成复用推挽输出，用来把串口数据发给板载 CH340。 */
    gpio.GPIO_Pin = GPIO_Pin_9;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    /* PA10 是串口接收脚，使用浮空输入，外部 CH340 会驱动电平。 */
    gpio.GPIO_Pin = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    USART_StructInit(&usart);
    usart.USART_BaudRate = APP_PROTOCOL_BAUDRATE;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &usart);

    /* RXNE 中断表示收到一个字节，ISR 会把字节放进 s_rx_line。 */
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    nvic.NVIC_IRQChannel = USART1_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1U;
    nvic.NVIC_IRQChannelSubPriority = 0U;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    s_rx_len = 0U;
    s_pending_ready = 0U;
    s_observed_rx_len = 0U;
    s_rx_idle_start_us = 0U;

    USART_Cmd(USART1, ENABLE);
    app_protocol_send_line("OK COURSE1 READY");
}

uint8_t app_protocol_task(uint32_t now_us)
{
    char cmd[APP_PROTOCOL_LINE_MAX];
    uint8_t monitor_changed = 0U;
    uint8_t i;
    uint8_t rx_len;

    if (s_pending_ready != 0U) {
        /*
         * pending_line 由中断和主循环共享，复制时短暂关中断，防止复制到一半
         * 又被新的串口字节修改。
         */
        __disable_irq();
        for (i = 0U; i < APP_PROTOCOL_LINE_MAX; i++) {
            cmd[i] = (char)s_pending_line[i];
            if (cmd[i] == '\0') {
                break;
            }
        }
        cmd[APP_PROTOCOL_LINE_MAX - 1U] = '\0';
        s_pending_ready = 0U;
        __enable_irq();
        monitor_changed = dispatch_pending_line(cmd);
    }

    __disable_irq();
    rx_len = s_rx_len;
    __enable_irq();

    if (rx_len == 0U) {
        /* 没有正在输入的行，idle 计时也要清零。 */
        s_observed_rx_len = 0U;
        s_rx_idle_start_us = 0U;
    } else {
        if (rx_len != s_observed_rx_len) {
            /* 缓冲区长度变化，说明用户还在输入，重新开始 idle 计时。 */
            s_observed_rx_len = rx_len;
            s_rx_idle_start_us = now_us;
        } else if ((now_us - s_rx_idle_start_us) >= APP_PROTOCOL_IDLE_TIMEOUT_US) {
            /* 一段时间没有新字节，就按当前内容执行命令。 */
            __disable_irq();
            if (s_rx_len > 0U && s_pending_ready == 0U) {
                queue_rx_line();
                s_observed_rx_len = 0U;
                s_rx_idle_start_us = 0U;
            }
            __enable_irq();
        }
    }

    return monitor_changed;
}

void app_protocol_usart1_irq_handler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        char c = (char)(USART_ReceiveData(USART1) & 0xFFU);

        /*
         * 回车或换行代表一条命令结束。空行直接忽略，避免产生 BAD_COMMAND。
         */
        if (c == '\r' || c == '\n') {
            if (s_rx_len > 0U) {
                queue_rx_line();
            }
        } else if (c >= 32 && c <= 126) {
            /* 只接收可打印 ASCII 字符，方便串口助手直接输入命令。 */
            if (s_rx_len < (APP_PROTOCOL_LINE_MAX - 1U)) {
                s_rx_line[s_rx_len++] = c;
            } else {
                /* 行太长时丢弃整行，避免半截命令被误执行。 */
                s_rx_len = 0U;
            }
        }
    }
}

void app_protocol_send_line(const char *line)
{
    /* 每条响应后面都补 CRLF，串口助手通常会按一行显示。 */
    while (*line != '\0') {
        usart_send_char(*line++);
    }
    usart_send_char('\r');
    usart_send_char('\n');
}
