/**
 * @file app_adc.c
 * @brief ADC 采样与电气参数计算模块实现。
 *
 * 三层快照架构（解决 DMA ISR / 主循环 / LCD-串口 数据竞争）：
 *   第一层  DMA HT/TC 中断 → deinterleave_frame() 写 ISR 双缓冲 s_adc_frame[2]
 *   第二层  主循环 app_adc_task() 极短临界区取 ready idx → 整帧复制到工作帧
 *           → 调用 Rust 算法 → 复制到显示帧
 *   第三层  app_adc_get_samples() / app_adc_calibrate_zero() 统一读显示帧
 *
 * 硬件链路：
 *   TIM3 (6400 Hz TRGO) → ADC1 扫描 PC0/PC3/PC2
 *   → DMA1_Channel1 循环搬运 2×384 半字到 s_dma_buffer
 *   → DMA HT 中断：解交错前半帧；TC 中断：解交错后半帧
 *   → app_adc_task() (主循环中)：临界区取帧号 → 工作帧 → Rust 算法 → 显示帧
 *
 * 校准链路：
 *   CAL ZERO 命令 → app_adc_calibrate_zero()
 *   → 取显示帧各 128 点算术平均 → 写入 s_zero_*
 *   → Rust 函数在下次调用时使用这些 DC 偏移值
 */
#include "app_adc.h"
#include "rust_algos.h"

#include "misc.h"
#include "stm32f10x_adc.h"
#include "stm32f10x_dma.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_tim.h"

/* ================================================================== */
/* 常量                                                               */
/* ================================================================== */

/** DMA 缓冲区帧长 = 通道数 × 每通道点数 = 3 × 128 = 384 半字。 */
#define APP_ADC_FRAME_HALFWORDS (APP_ADC_SAMPLES * APP_ADC_CHANNELS)

/* ================================================================== */
/* 静态变量                                                           */
/* ================================================================== */

typedef struct {
    uint16_t v[APP_ADC_SAMPLES];
    uint16_t i[APP_ADC_SAMPLES];
    uint16_t ilk[APP_ADC_SAMPLES];
} app_adc_frame_t;

/*
 * DMA 2 倍帧长循环缓冲区（768 半字）。
 * DMA 以 circular 模式持续写入，HT 处理前半帧 [0..384)，TC 处理后半帧 [384..768)。
 * 不需要修改 CMAR，ISR 与 DMA 不会竞争同一半区。
 */
static uint16_t s_dma_buffer[2U * APP_ADC_FRAME_HALFWORDS];

/*
 * 数据所有权：
 * - ISR 写 s_adc_frame[s_adc_write_idx]，读取 s_dma_buffer 后解交错写入；
 * - 主循环读 s_adc_frame[s_adc_ready_idx]，极短临界区只取 ready 帧编号；
 * - s_adc_ready / s_adc_ready_idx / s_adc_write_idx 均为 volatile，
 *   主循环在读 ready_idx 前关中断避免 ISR 同时切换 write/ready。
 */
static app_adc_frame_t s_adc_frame[2];
static volatile uint8_t s_adc_write_idx;
static volatile uint8_t s_adc_ready_idx;
static volatile uint8_t s_adc_ready;

/** 主循环工作帧：从 ready 帧复制，用于 Rust 计算。 */
static app_adc_frame_t s_adc_work_frame;

/** 显示帧：LCD、串口、CAL ZERO 统一读取的稳定快照。 */
static app_adc_frame_t s_adc_display_frame;

/** Rust 算法库填充的电参数结果。 */
static app_electrical_params_t s_electrical_params;

/** 零偏移校准 DC 补偿值（0 = 自动估算）。 */
static uint16_t s_zero_v;
static uint16_t s_zero_i;
static uint16_t s_zero_ilk;

/** 是否已执行过 CAL ZERO。 */
static uint8_t s_zero_calibrated;

/* ================================================================== */
/* 定时器参数计算（与 app_dac.c 中 calc_timer_params 算法一致）       */
/* ================================================================== */

/**
 * @brief 根据目标触发频率计算 TIM3 的 PSC 和 ARR。
 *
 * @param rate_hz  目标触发频率，单位 Hz。
 * @param[out] psc 预分频器值（写入 TIM_Prescaler）。
 * @param[out] arr 自动重装载值（写入 TIM_Period）。
 */
static void calc_adc_timer_params(uint32_t rate_hz,
                                  uint16_t *psc,
                                  uint16_t *arr)
{
    uint32_t target_ticks =
        (APP_APB1_TIMER_CLK_HZ + (rate_hz / 2U)) / rate_hz;
    uint32_t prescaler = (target_ticks + 65535U) / 65536U;
    uint32_t period;

    if (prescaler > 0U) {
        prescaler--;
    }
    if (prescaler > 65535U) {
        prescaler = 65535U;
    }

    period = ((APP_APB1_TIMER_CLK_HZ / (prescaler + 1U)) + (rate_hz / 2U)) / rate_hz;
    if (period < 2U) {
        period = 2U;
    }
    if (period > 65536U) {
        period = 65536U;
    }

    *psc = (uint16_t)prescaler;
    *arr = (uint16_t)(period - 1U);
}

/* ================================================================== */
/* 初始化                                                             */
/* ================================================================== */

/**
 * @brief 初始化 ADC 采样硬件链路。
 *
 * ADC1 由 TIM3 TRGO 触发，每次触发完成 3 通道规则组扫描：
 *   Rank1: PC0 / ADC1_IN10 / VL
 *   Rank2: PC3 / ADC1_IN13 / iL
 *   Rank3: PC2 / ADC1_IN12 / iLK
 *
 * 采样率 = 50 Hz × 128 点 = 6400 组/s。
 * 采样时间取 239.5 cycles ≈ 21 μs @ 12 MHz，用较长采样窗口降低外部调理电路输出阻抗影响。
 */
void app_adc_init(void)
{
    /* --- 1. RCC 时钟 --- */
    /* ADC 时钟 ≤ 14 MHz：72 / 6 = 12 MHz */
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_ADC1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    /* --- 2. GPIO：PC0/PC3/PC2 模拟输入 --- */
    {
        GPIO_InitTypeDef gpio;
        GPIO_StructInit(&gpio);
        gpio.GPIO_Pin  = GPIO_Pin_0 | GPIO_Pin_3 | GPIO_Pin_2;
        gpio.GPIO_Mode = GPIO_Mode_AIN;
        GPIO_Init(GPIOC, &gpio);
    }

    /* --- 3. TIM3：6400 Hz TRGO 触发源 --- */
    {
        uint16_t psc, arr;
        calc_adc_timer_params(APP_ADC_SAMPLE_RATE_HZ, &psc, &arr);

        TIM_TimeBaseInitTypeDef time_base;
        TIM_TimeBaseStructInit(&time_base);
        time_base.TIM_Prescaler     = psc;
        time_base.TIM_Period        = arr;
        time_base.TIM_CounterMode   = TIM_CounterMode_Up;
        time_base.TIM_ClockDivision = TIM_CKD_DIV1;
        TIM_TimeBaseInit(TIM3, &time_base);

        TIM_SelectOutputTrigger(TIM3, TIM_TRGOSource_Update);
    }

    /* --- 4. ADC1：独立模式，非连续扫描 3 通道，TIM3 TRGO 触发 --- */
    {
        ADC_InitTypeDef adc;
        ADC_StructInit(&adc);
        adc.ADC_Mode               = ADC_Mode_Independent;
        adc.ADC_ScanConvMode       = ENABLE;
        adc.ADC_ContinuousConvMode = DISABLE;
        adc.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_T3_TRGO;
        adc.ADC_DataAlign          = ADC_DataAlign_Right;
        adc.ADC_NbrOfChannel       = APP_ADC_CHANNELS;
        ADC_Init(ADC1, &adc);

        ADC_RegularChannelConfig(ADC1, APP_ADC_V_CHANNEL,   1, ADC_SampleTime_239Cycles5);
        ADC_RegularChannelConfig(ADC1, APP_ADC_I_CHANNEL,   2, ADC_SampleTime_239Cycles5);
        ADC_RegularChannelConfig(ADC1, APP_ADC_ILK_CHANNEL, 3, ADC_SampleTime_239Cycles5);

        ADC_DMACmd(ADC1, ENABLE);
        ADC_ExternalTrigConvCmd(ADC1, ENABLE);

        ADC_Cmd(ADC1, ENABLE);
        ADC_ResetCalibration(ADC1);
        while (ADC_GetResetCalibrationStatus(ADC1) != RESET) {}
        ADC_StartCalibration(ADC1);
        while (ADC_GetCalibrationStatus(ADC1) != RESET) {}
    }

    /* --- 5. DMA1_Channel1：外设→内存，2 倍帧循环，HT/TC 中断 --- */
    {
        DMA_InitTypeDef dma;
        DMA_DeInit(DMA1_Channel1);
        DMA_StructInit(&dma);
        dma.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->DR;
        dma.DMA_MemoryBaseAddr     = (uint32_t)s_dma_buffer;
        dma.DMA_DIR                = DMA_DIR_PeripheralSRC;
        dma.DMA_BufferSize         = 2U * APP_ADC_FRAME_HALFWORDS;
        dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
        dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
        dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
        dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
        dma.DMA_Mode               = DMA_Mode_Circular;
        dma.DMA_Priority           = DMA_Priority_High;
        dma.DMA_M2M                = DMA_M2M_Disable;
        DMA_Init(DMA1_Channel1, &dma);

        DMA_ITConfig(DMA1_Channel1, DMA_IT_HT | DMA_IT_TC, ENABLE);
    }

    /* --- 6. NVIC --- */
    {
        NVIC_InitTypeDef nvic;
        nvic.NVIC_IRQChannel                   = DMA1_Channel1_IRQn;
        nvic.NVIC_IRQChannelPreemptionPriority = 1U;
        nvic.NVIC_IRQChannelSubPriority        = 2U;
        nvic.NVIC_IRQChannelCmd                = ENABLE;
        NVIC_Init(&nvic);
    }

    /* --- 7. 启动链路：DMA → ADC → TIM3 --- */
    DMA_Cmd(DMA1_Channel1, ENABLE);
    TIM_Cmd(TIM3, ENABLE);

    /* --- 8. 校准与双缓冲初始化 --- */
    s_adc_write_idx   = 0U;
    s_adc_ready_idx   = 0U;
    s_adc_ready       = 0U;
    s_zero_v          = 0U;
    s_zero_i          = 0U;
    s_zero_ilk        = 0U;
    s_zero_calibrated = 0U;
}

/* ================================================================== */
/* DMA HT/TC 中断处理                                                 */
/* ================================================================== */

/**
 * @brief 从 DMA 缓冲区指定偏移处解交错一帧到 ISR 写缓冲。
 *
 * DMA 输入为交错序列：V1, I1, ILK1, V2, I2, ILK2, ...
 * 输出为三个独立的连续数组。
 *
 * 写入当前 s_adc_write_idx 后切换 write/ready，DMA 始终写另一面。
 */
static void deinterleave_frame(const uint16_t *src)
{
    uint8_t wi = s_adc_write_idx;
    uint16_t k;

    for (k = 0U; k < APP_ADC_SAMPLES; k++) {
        s_adc_frame[wi].v[k]   = src[k * 3U];
        s_adc_frame[wi].i[k]   = src[k * 3U + 1U];
        s_adc_frame[wi].ilk[k] = src[k * 3U + 2U];
    }

    s_adc_ready_idx = wi;
    s_adc_write_idx = (uint8_t)(wi ^ 1U);
    s_adc_ready     = 1U;
}

/**
 * @brief DMA1_Channel1 半传输/传输完成中断。
 *
 * HT：前半帧 [0..384) 写完 → 安全读；TC：后半帧 [384..768) 写完 → 安全读。
 * DMA circular 模式下地址自动回绕，无需操作 CMAR。
 */
void app_adc_dma1_ch1_irq_handler(void)
{
    if (DMA_GetITStatus(DMA1_IT_HT1) != RESET) {
        DMA_ClearITPendingBit(DMA1_IT_HT1);
        deinterleave_frame(&s_dma_buffer[0]);
    }

    if (DMA_GetITStatus(DMA1_IT_TC1) != RESET) {
        DMA_ClearITPendingBit(DMA1_IT_TC1);
        deinterleave_frame(&s_dma_buffer[APP_ADC_FRAME_HALFWORDS]);
    }
}

/* ================================================================== */
/* 主循环任务                                                         */
/* ================================================================== */

/**
 * @brief 主循环中检查新数据并触发电参数计算。
 *
 * 流程：
 * 1. 极短临界区（关中断仅取 ready 帧编号）；
 * 2. 整帧复制到工作帧（临界区外，不阻塞中断）；
 * 3. 快照校准参数（避免 Rust 函数执行期间被 CAL ZERO 修改）；
 * 4. 调用 Rust 定点数算法库计算电参数；
 * 5. 更新显示帧快照，供 LCD/串口/CAL ZERO 安全读取。
 */
uint8_t app_adc_task(void)
{
    uint8_t ri;

    /* 极短临界区：只取 ready 帧编号并清标志 */
    __disable_irq();
    if (s_adc_ready == 0U) {
        __enable_irq();
        return 0U;
    }
    ri = s_adc_ready_idx;
    s_adc_ready = 0U;
    __enable_irq();

    /* 整帧复制到工作缓冲（不在临界区内，不占用栈） */
    s_adc_work_frame = s_adc_frame[ri];

    /* 快照校准参数 */
    uint16_t cal_v    = s_zero_v;
    uint16_t cal_i    = s_zero_i;
    uint16_t cal_ilk  = s_zero_ilk;
    uint8_t  cal_done = s_zero_calibrated;

    /* 调用 Rust 定点数算法库计算电参数 */
    pm_calc_electrical(
        s_adc_work_frame.v,
        s_adc_work_frame.i,
        s_adc_work_frame.ilk,
        APP_ADC_SAMPLES,
        (uint32_t)APP_ADC_CALIB_V_GAIN_X1000,
        (uint32_t)APP_ADC_CALIB_I_GAIN_X1000,
        (uint32_t)APP_ADC_CALIB_ILK_GAIN_X1000,
        cal_v,
        cal_i,
        cal_ilk,
        (int8_t)APP_ADC_I_POLARITY,
        (struct PmElectricalResult *)&s_electrical_params);

    s_electrical_params.zero_calibrated = cal_done;

    /* 更新显示帧快照 */
    s_adc_display_frame = s_adc_work_frame;

    return 1U;
}

/* ================================================================== */
/* 公共接口                                                           */
/* ================================================================== */

const app_electrical_params_t *app_adc_get_params(void)
{
    return &s_electrical_params;
}

const uint16_t *app_adc_get_samples(uint8_t channel)
{
    if (channel == 0U) {
        return s_adc_display_frame.v;
    }
    if (channel == 1U) {
        return s_adc_display_frame.i;
    }
    if (channel == 2U) {
        return s_adc_display_frame.ilk;
    }
    return (const uint16_t *)0;
}

/**
 * @brief 执行零偏移校准：取显示帧 128 点算术平均作为 DC 偏移。
 *
 * 基于 s_adc_display_frame（稳定快照），不受 ISR 写入干扰。
 * 在主循环上下文中调用（CAL ZERO 命令处理），非 ISR 上下文。
 */
void app_adc_calibrate_zero(void)
{
    const app_adc_frame_t *f = &s_adc_display_frame;
    uint32_t sum_v   = 0U;
    uint32_t sum_i   = 0U;
    uint32_t sum_ilk = 0U;
    uint16_t j;

    for (j = 0U; j < APP_ADC_SAMPLES; j++) {
        sum_v   += (uint32_t)f->v[j];
        sum_i   += (uint32_t)f->i[j];
        sum_ilk += (uint32_t)f->ilk[j];
    }

    s_zero_v   = (uint16_t)(sum_v   / APP_ADC_SAMPLES);
    s_zero_i   = (uint16_t)(sum_i   / APP_ADC_SAMPLES);
    s_zero_ilk = (uint16_t)(sum_ilk / APP_ADC_SAMPLES);
    s_zero_calibrated = 1U;
}
