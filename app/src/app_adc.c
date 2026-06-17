/**
 * @file app_adc.c
 * @brief ADC 采样与电气参数计算模块实现。
 *
 * 硬件链路：
 *   TIM3 (6400 Hz TRGO) → ADC1 扫描 PC0/PC1/PC2
 *   → DMA1_Channel1 循环搬运 384 半字到 s_dma_buffer
 *   → DMA TC 中断：解交错到 s_adc_v/i/ilk[128]
 *   → app_adc_task() (主循环中)：复制到局部缓冲 → Rust 算法 → 快照更新
 *
 * 校准链路：
 *   CAL ZERO 命令 → app_adc_calibrate_zero()
 *   → 取当前 s_adc_* 各 128 点算术平均 → 写入 s_zero_*
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
/* 硬件常量                                                           */
/* ================================================================== */

/** APB1 定时器时钟频率，STM32F103 默认 APB1=36MHz 时定时器时钟=72MHz。 */
#define APP_ADC_TIMER_CLOCK_HZ 72000000U

/** ADC 触发采样率：50 Hz × 128 点/周期 = 6400 组/秒。 */
#define APP_ADC_SAMPLE_RATE_HZ 6400U

/** DMA 缓冲区大小 = 通道数 × 每通道点数 = 3 × 128 = 384。 */
#define APP_ADC_DMA_SIZE (APP_ADC_SAMPLES * APP_ADC_CHANNELS)

/* ================================================================== */
/* 静态变量                                                           */
/* ================================================================== */

/**
 * @brief DMA 循环目标缓冲区。
 *
 * DMA1_Channel1 从 ADC1->DR 搬运到这个数组，顺序为
 * V[0] I[0] Ilk[0] V[1] I[1] Ilk[1] ... V[127] I[127] Ilk[127]。
 */
static uint16_t s_dma_buffer[APP_ADC_DMA_SIZE];

/** 解交错后的电压通道样点（PC0 / ADC1_IN10）。 */
static uint16_t s_adc_v[APP_ADC_SAMPLES];

/** 解交错后的电流通道样点（PC1 / ADC1_IN11）。 */
static uint16_t s_adc_i[APP_ADC_SAMPLES];

/** 解交错后的漏电流通道样点（PC2 / ADC1_IN12）。 */
static uint16_t s_adc_ilk[APP_ADC_SAMPLES];

/** DMA TC 中断置 1，app_adc_task() 消费后清零。 */
static volatile uint8_t s_adc_fresh;

/** 由 Rust 算法库计算后填充的电参数结果。 */
static app_electrical_params_t s_electrical_params;

/** 零偏移校准时写入的 DC 偏移值（0 = 自动估算）。 */
static uint16_t s_zero_v;
static uint16_t s_zero_i;
static uint16_t s_zero_ilk;

/** 是否已执行过 CAL ZERO。写入校准时置 1。 */
static uint8_t s_zero_calibrated;

/* ================================================================== */
/* 定时器参数计算（与 app_dac.c 中 calc_timer_params 算法一致）         */
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
    // 单次触发周期需要的定时器 tick 数（四舍五入）
    uint32_t target_ticks =
        (APP_ADC_TIMER_CLOCK_HZ + (rate_hz / 2U)) / rate_hz;
    // 先算需要的预分频器值
    uint32_t prescaler = (target_ticks + 65535U) / 65536U;
    // 周期计数值（ARR + 1 即实际一个触发周期的 tick 数）
    uint32_t period;

    if (prescaler > 0U) {
        prescaler--;
    }
    if (prescaler > 65535U) {
        prescaler = 65535U;
    }

    period = ((APP_ADC_TIMER_CLOCK_HZ / (prescaler + 1U)) + (rate_hz / 2U)) / rate_hz;
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
 */
void app_adc_init(void)
{
    /* --- 1. RCC 时钟 --- */
    // ADC 时钟 ≤ 14 MHz：72 / 6 = 12 MHz
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    // GPIOC（PC0-PC2 模拟输入）和 ADC1 在 APB2 上
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_ADC1,
                           ENABLE);
    // TIM3 在 APB1 上
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    // DMA1 在 AHB 上
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    /* --- 2. GPIO: PC0/PC1/PC2 模拟输入 --- */
    {
        GPIO_InitTypeDef gpio;
        GPIO_StructInit(&gpio);
        gpio.GPIO_Pin  = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2;
        gpio.GPIO_Mode = GPIO_Mode_AIN;
        GPIO_Init(GPIOC, &gpio);
    }

    /* --- 3. TIM3: 6400 Hz TRGO 触发源 --- */
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

        // TIM3 Update 事件作为 TRGO，接到 ADC1 的外部触发
        TIM_SelectOutputTrigger(TIM3, TIM_TRGOSource_Update);
    }

    /* --- 4. ADC1: 独立模式，扫描 3 通道，TIM3 TRGO 触发 --- */
    {
        ADC_InitTypeDef adc;
        ADC_StructInit(&adc);
        adc.ADC_Mode               = ADC_Mode_Independent;
        adc.ADC_ScanConvMode       = ENABLE;
        // 非连续转换：每次 TRGO 只触发一次完整扫描序列
        adc.ADC_ContinuousConvMode = DISABLE;
        adc.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_T3_TRGO;
        adc.ADC_DataAlign          = ADC_DataAlign_Right;
        adc.ADC_NbrOfChannel       = APP_ADC_CHANNELS;
        ADC_Init(ADC1, &adc);

        // 规则组顺序：Rank1=IN10(VL), Rank2=IN11(iL), Rank3=IN12(iLK)
        // 每通道 55.5 周期采样时间 ≈ 5.6 μs @ 12 MHz
        ADC_RegularChannelConfig(ADC1,
                                 ADC_Channel_10,
                                 1,
                                 ADC_SampleTime_55Cycles5);
        ADC_RegularChannelConfig(ADC1,
                                 ADC_Channel_11,
                                 2,
                                 ADC_SampleTime_55Cycles5);
        ADC_RegularChannelConfig(ADC1,
                                 ADC_Channel_12,
                                 3,
                                 ADC_SampleTime_55Cycles5);

        // 使能 DMA 请求和外部触发
        ADC_DMACmd(ADC1, ENABLE);
        ADC_ExternalTrigConvCmd(ADC1, ENABLE);

        // 上电并校准
        ADC_Cmd(ADC1, ENABLE);
        ADC_ResetCalibration(ADC1);
        while (ADC_GetResetCalibrationStatus(ADC1) != RESET) {}
        ADC_StartCalibration(ADC1);
        while (ADC_GetCalibrationStatus(ADC1) != RESET) {}
    }

    /* --- 5. DMA1_Channel1: 外设→内存，循环模式 --- */
    {
        DMA_InitTypeDef dma;
        DMA_DeInit(DMA1_Channel1);
        DMA_StructInit(&dma);
        dma.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->DR;
        dma.DMA_MemoryBaseAddr     = (uint32_t)s_dma_buffer;
        dma.DMA_DIR                = DMA_DIR_PeripheralSRC;
        dma.DMA_BufferSize         = APP_ADC_DMA_SIZE;
        dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
        dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
        dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
        dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
        dma.DMA_Mode               = DMA_Mode_Circular;
        dma.DMA_Priority           = DMA_Priority_High;
        dma.DMA_M2M                = DMA_M2M_Disable;
        DMA_Init(DMA1_Channel1, &dma);

        // 使能传输完成中断
        DMA_ITConfig(DMA1_Channel1, DMA_IT_TC, ENABLE);
    }

    /* --- 6. NVIC: DMA1_Channel1 中断 --- */
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
    // TIM3 启动后立刻开始产生 TRGO，触发第一次扫描序列
    TIM_Cmd(TIM3, ENABLE);

    /* --- 8. 校准状态初始化 --- */
    s_adc_fresh       = 0U;
    s_zero_v          = 0U;
    s_zero_i          = 0U;
    s_zero_ilk        = 0U;
    s_zero_calibrated = 0U;
}

/* ================================================================== */
/* DMA TC 中断处理                                                      */
/* ================================================================== */

/**
 * @brief DMA1_Channel1 传输完成中断入口。
 *
 * 在中断上下文中运行，只做三件事：
 * 1. 清除 TC 标志。
 * 2. 从 s_dma_buffer（交错 V/I/Ilk）解交错到 s_adc_v/i/ilk。
 * 3. 置 s_adc_fresh = 1，通知主循环有新数据。
 */
void app_adc_dma1_ch1_irq_handler(void)
{
    if (DMA_GetITStatus(DMA1_IT_TC1) != RESET) {
        DMA_ClearITPendingBit(DMA1_IT_TC1);

        // 从 384 点交错缓冲区解交错到 3 个 128 点通道数组
        uint16_t i;
        for (i = 0U; i < APP_ADC_SAMPLES; i++) {
            s_adc_v[i]   = s_dma_buffer[i * 3U];
            s_adc_i[i]   = s_dma_buffer[i * 3U + 1U];
            s_adc_ilk[i] = s_dma_buffer[i * 3U + 2U];
        }

        s_adc_fresh = 1U;
    }
}

/* ================================================================== */
/* 主循环任务                                                          */
/* ================================================================== */

/**
 * @brief 在主循环中调用：检查新数据并触发电参数计算。
 */
void app_adc_task(void)
{
    if (s_adc_fresh == 0U) {
        return;
    }

    // 将 ISR 填充的 s_adc_* 复制到局部缓冲。
    // DMA 仍在 circular 模式下持续覆盖 s_dma_buffer，但 s_adc_* 只在
    // ISR 中更新。fresh 清零后，ISR 下一次置 1 前这些局部数据不会被改。
    uint16_t local_v[APP_ADC_SAMPLES];
    uint16_t local_i[APP_ADC_SAMPLES];
    uint16_t local_ilk[APP_ADC_SAMPLES];
    uint16_t k;

    for (k = 0U; k < APP_ADC_SAMPLES; k++) {
        local_v[k]   = s_adc_v[k];
        local_i[k]   = s_adc_i[k];
        local_ilk[k] = s_adc_ilk[k];
    }

    // 快照校准参数（避免在 Rust 函数调用期间被 CAL ZERO 命令修改）
    uint16_t cal_v   = s_zero_v;
    uint16_t cal_i   = s_zero_i;
    uint16_t cal_ilk = s_zero_ilk;
    uint8_t  cal_done = s_zero_calibrated;

    // 标志清零放在数据复制之后，防止 ISR 在复制过程中覆盖 s_adc_*
    s_adc_fresh = 0U;

    // 调用 Rust 定点数算法库计算电参数
    pm_calc_electrical(
        local_v,
        local_i,
        local_ilk,
        APP_ADC_SAMPLES,
        (uint32_t)APP_ADC_CALIB_V_GAIN_X1000,
        (uint32_t)APP_ADC_CALIB_I_GAIN_X1000,
        (uint32_t)APP_ADC_CALIB_ILK_GAIN_X1000,
        cal_v,
        cal_i,
        cal_ilk,
        (struct PmElectricalResult *)&s_electrical_params);

    s_electrical_params.zero_calibrated = cal_done;
}

/* ================================================================== */
/* 公共接口                                                            */
/* ================================================================== */

/**
 * @brief 获取最近一次电参数计算结果。
 */
const app_electrical_params_t *app_adc_get_params(void)
{
    return &s_electrical_params;
}

/**
 * @brief 获取指定通道的原始 ADC 样点数组。
 */
const uint16_t *app_adc_get_samples(uint8_t channel)
{
    if (channel == 0U) {
        return s_adc_v;
    }
    if (channel == 1U) {
        return s_adc_i;
    }
    if (channel == 2U) {
        return s_adc_ilk;
    }
    return (const uint16_t *)0;
}

/**
 * @brief 执行零偏移校准：取当前一轮样点的算术平均作为 DC 偏移。
 *
 * 在 CAL ZERO 命令的处理函数中调用（主循环上下文，非 ISR 上下文）。
 * 此时 s_adc_v/i/ilk 必须已有有效数据（fresh 刚被消费过）。
 */
void app_adc_calibrate_zero(void)
{
    uint32_t sum_v   = 0U;
    uint32_t sum_i   = 0U;
    uint32_t sum_ilk = 0U;
    uint16_t j;

    for (j = 0U; j < APP_ADC_SAMPLES; j++) {
        sum_v   += (uint32_t)s_adc_v[j];
        sum_i   += (uint32_t)s_adc_i[j];
        sum_ilk += (uint32_t)s_adc_ilk[j];
    }

    s_zero_v   = (uint16_t)(sum_v   / APP_ADC_SAMPLES);
    s_zero_i   = (uint16_t)(sum_i   / APP_ADC_SAMPLES);
    s_zero_ilk = (uint16_t)(sum_ilk / APP_ADC_SAMPLES);
    s_zero_calibrated = 1U;
}
