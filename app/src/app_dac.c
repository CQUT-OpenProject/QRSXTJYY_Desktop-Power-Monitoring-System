#include "app_dac.h"

#include "app_config.h"
#include "stm32f10x_dac.h"
#include "stm32f10x_dma.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_tim.h"

/* 硬件常量来自 app_config.h：APP_APB1_TIMER_CLK_HZ / APP_DAC_TABLE_SIZE 等 */

/*
 * 一整周期 128 点满幅正弦表（0..4095），按用户幅度缩放，不在运行时计算 sin()。
 * 预计算后按 amplitude 和 phase 重建 s_dac_ch1 / s_dac_ch2 / s_dac_dual。
 */
static const uint16_t s_sine_fullscale[APP_DAC_TABLE_SIZE] = {
    2048U, 2148U, 2249U, 2348U, 2447U, 2545U, 2642U, 2738U,
    2831U, 2923U, 3013U, 3100U, 3185U, 3267U, 3347U, 3423U,
    3495U, 3565U, 3630U, 3692U, 3750U, 3804U, 3853U, 3898U,
    3939U, 3975U, 4007U, 4034U, 4056U, 4073U, 4085U, 4093U,
    4095U, 4093U, 4085U, 4073U, 4056U, 4034U, 4007U, 3975U,
    3939U, 3898U, 3853U, 3804U, 3750U, 3692U, 3630U, 3565U,
    3495U, 3423U, 3347U, 3267U, 3185U, 3100U, 3013U, 2923U,
    2831U, 2738U, 2642U, 2545U, 2447U, 2348U, 2249U, 2148U,
    2048U, 1948U, 1847U, 1748U, 1649U, 1551U, 1454U, 1358U,
    1265U, 1173U, 1083U, 996U, 911U, 829U, 749U, 673U,
    601U, 531U, 466U, 404U, 346U, 292U, 243U, 198U,
    157U, 121U, 89U, 62U, 40U, 23U, 11U, 3U,
    1U, 3U, 11U, 23U, 40U, 62U, 89U, 121U,
    157U, 198U, 243U, 292U, 346U, 404U, 466U, 531U,
    601U, 673U, 749U, 829U, 911U, 996U, 1083U, 1173U,
    1265U, 1358U, 1454U, 1551U, 1649U, 1748U, 1847U, 1948U
};

static uint16_t s_dac_ch1[APP_DAC_TABLE_SIZE];
static uint16_t s_dac_ch2[APP_DAC_TABLE_SIZE];

static app_dac_config_t s_config = {
    APP_DAC_MODE_SINGLE,
    APP_DAC_DEFAULT_FREQ_HZ,
    APP_DAC_DEFAULT_FREQ_HZ,
    APP_DAC_DEFAULT_AMPLITUDE,
    APP_DAC_DEFAULT_AMPLITUDE,
    0U,
    0U
};

/** 将频率限制到 DAC 可输出范围。 */
static uint32_t clamp_frequency(uint32_t hz)
{
    if (hz < APP_DAC_MIN_HZ) {
        return APP_DAC_MIN_HZ;
    }
    if (hz > APP_DAC_MAX_FREQ_HZ) {
        return APP_DAC_MAX_FREQ_HZ;
    }
    return hz;
}

/** 将幅度限制到 12 位 DAC 安全码值。 */
static uint16_t clamp_amplitude(uint16_t code)
{
    return code > APP_DAC_MAX_AMP ? APP_DAC_MAX_AMP : code;
}

/**
 * @brief 按目标幅度缩放一个满幅正弦样点。
 *
 * raw 以 2048 为中心上下摆动。先转成正负偏移量，按 amplitude 缩放再加回 2048。
 */
static uint16_t scale_sample(uint16_t raw, uint16_t amplitude)
{
    int32_t signed_sample = (int32_t)raw - (int32_t)APP_DAC_MID_CODE;
    int32_t scaled = (int32_t)APP_DAC_MID_CODE +
                     ((signed_sample * (int32_t)amplitude) / (int32_t)APP_DAC_MAX_AMP);

    if (scaled < 0) {
        return 0U;
    }
    if (scaled > 4095) {
        return 4095U;
    }
    return (uint16_t)scaled;
}

/** 根据当前配置重建两路波形表。 */
static void rebuild_buffers(void)
{
    uint16_t i;
    uint16_t phase_index_ch1 =
        (uint16_t)(((uint32_t)s_config.phase_degrees * APP_DAC_TABLE_SIZE) / 360U);
    uint16_t phase_index_ch2 =
        (uint16_t)(((uint32_t)s_config.phase_degrees_ch2 * APP_DAC_TABLE_SIZE) / 360U);

    for (i = 0U; i < APP_DAC_TABLE_SIZE; i++) {
        uint16_t idx1 = (uint16_t)((i + phase_index_ch1) % APP_DAC_TABLE_SIZE);
        s_dac_ch1[i] = scale_sample(s_sine_fullscale[idx1], s_config.amplitude);
        if (s_config.mode == APP_DAC_MODE_DUAL) {
            uint16_t idx2 = (uint16_t)((i + phase_index_ch2) % APP_DAC_TABLE_SIZE);
            s_dac_ch2[i] = scale_sample(s_sine_fullscale[idx2], s_config.amplitude_ch2);
        } else {
            s_dac_ch2[i] = APP_DAC_MID_CODE;
        }
    }
}

/**
 * @brief 根据目标采样率计算 TIM6 的 PSC 和 ARR。
 *
 * 采样率 = 波形频率 × 每周期样点数。TIM6 Update 触发 DMA 写一个采样点。
 */
static void calc_timer_params(uint32_t sample_rate_hz, uint16_t *psc, uint16_t *arr)
{
    uint32_t target_ticks = (APP_APB1_TIMER_CLK_HZ + (sample_rate_hz / 2U)) / sample_rate_hz;
    uint32_t prescaler = (target_ticks + 65535U) / 65536U;
    uint32_t period;

    if (prescaler > 0U) {
        prescaler--;
    }
    if (prescaler > 65535U) {
        prescaler = 65535U;
    }

    period = ((APP_APB1_TIMER_CLK_HZ / (prescaler + 1U)) + (sample_rate_hz / 2U)) / sample_rate_hz;
    if (period < 2U) {
        period = 2U;
    }
    if (period > 65536U) {
        period = 65536U;
    }

    *psc = (uint16_t)prescaler;
    *arr = (uint16_t)(period - 1U);
}

/** 根据当前模式配置 DAC 对应的 DMA 通道。 */
static void configure_dma(void)
{
    DMA_InitTypeDef dma;

    DMA_DeInit(DMA2_Channel3);
    DMA_DeInit(DMA2_Channel4);

    /* 通道1 DMA配置：DMA2_Channel3 → DAC->DHR12R1 */
    DMA_StructInit(&dma);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&DAC->DHR12R1;
    dma.DMA_MemoryBaseAddr = (uint32_t)s_dac_ch1;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize = APP_DAC_TABLE_SIZE;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode = DMA_Mode_Circular;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA2_Channel3, &dma);

    /* 通道2 DMA配置：DMA2_Channel4 → DAC->DHR12R2 */
    dma.DMA_PeripheralBaseAddr = (uint32_t)&DAC->DHR12R2;
    dma.DMA_MemoryBaseAddr = (uint32_t)s_dac_ch2;
    DMA_Init(DMA2_Channel4, &dma);
}

/** 停 DMA/TIM6 → 重建波形表 → 配置 DMA → 更新 TIM6 参数 → 重新启动。 */
static void apply_output_config(void)
{
    uint16_t psc1, arr1;
    uint16_t psc2, arr2;
    uint32_t sample_rate_hz1 = s_config.frequency_hz * APP_DAC_TABLE_SIZE;
    uint32_t sample_rate_hz2 = s_config.frequency_hz_ch2 * APP_DAC_TABLE_SIZE;

    TIM_Cmd(TIM6, DISABLE);
    TIM_Cmd(TIM7, DISABLE);
    DMA_Cmd(DMA2_Channel3, DISABLE);
    DMA_Cmd(DMA2_Channel4, DISABLE);

    rebuild_buffers();
    configure_dma();
    
    calc_timer_params(sample_rate_hz1, &psc1, &arr1);
    TIM_PrescalerConfig(TIM6, psc1, TIM_PSCReloadMode_Immediate);
    TIM_SetAutoreload(TIM6, arr1);
    TIM_GenerateEvent(TIM6, TIM_EventSource_Update);

    calc_timer_params(sample_rate_hz2, &psc2, &arr2);
    TIM_PrescalerConfig(TIM7, psc2, TIM_PSCReloadMode_Immediate);
    TIM_SetAutoreload(TIM7, arr2);
    TIM_GenerateEvent(TIM7, TIM_EventSource_Update);

    DMA_Cmd(DMA2_Channel3, ENABLE);
    DMA_Cmd(DMA2_Channel4, ENABLE);
    
    TIM_Cmd(TIM6, ENABLE);
    TIM_Cmd(TIM7, ENABLE);
}

/**
 * @brief 初始化 PA4/PA5 DAC 双通道输出。
 *
 * PA4(DAC_OUT1) / PA5(DAC_OUT2) 需配置为模拟输入模式。
 * TIM6 TRGO 触发 DMA 循环搬运波形表到 DAC 数据寄存器。
 */
void app_dac_init(void)
{
    GPIO_InitTypeDef gpio;
    DAC_InitTypeDef dac;
    TIM_TimeBaseInitTypeDef time_base;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC | RCC_APB1Periph_TIM6 | RCC_APB1Periph_TIM7, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5;
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &gpio);

    TIM_TimeBaseStructInit(&time_base);
    time_base.TIM_CounterMode = TIM_CounterMode_Up;
    time_base.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM6, &time_base);
    TIM_SelectOutputTrigger(TIM6, TIM_TRGOSource_Update);

    TIM_TimeBaseInit(TIM7, &time_base);
    TIM_SelectOutputTrigger(TIM7, TIM_TRGOSource_Update);

    DAC_StructInit(&dac);
    dac.DAC_Trigger = DAC_Trigger_T6_TRGO;
    dac.DAC_WaveGeneration = DAC_WaveGeneration_None;
    dac.DAC_LFSRUnmask_TriangleAmplitude = DAC_LFSRUnmask_Bit0;
    dac.DAC_OutputBuffer = DAC_OutputBuffer_Enable;
    DAC_Init(DAC_Channel_1, &dac);
    
    dac.DAC_Trigger = DAC_Trigger_T7_TRGO;
    DAC_Init(DAC_Channel_2, &dac);

    DAC_Cmd(DAC_Channel_1, ENABLE);
    DAC_Cmd(DAC_Channel_2, ENABLE);
    DAC_DMACmd(DAC_Channel_1, ENABLE);
    DAC_DMACmd(DAC_Channel_2, ENABLE);

    apply_output_config();
}

/** 归一化外部传入的配置：范围钳位 + 默认补齐。 */
static app_dac_config_t normalized_config(const app_dac_config_t *config)
{
    app_dac_config_t normalized = s_config;

    if (config != 0) {
        normalized = *config;
    }

    normalized.mode =
        normalized.mode == APP_DAC_MODE_DUAL ? APP_DAC_MODE_DUAL : APP_DAC_MODE_SINGLE;
    normalized.frequency_hz = clamp_frequency(normalized.frequency_hz);
    normalized.frequency_hz_ch2 = clamp_frequency(normalized.frequency_hz_ch2);
    normalized.amplitude = clamp_amplitude(normalized.amplitude);
    normalized.amplitude_ch2 = clamp_amplitude(normalized.amplitude_ch2);
    normalized.phase_degrees = (uint16_t)(normalized.phase_degrees % 360U);
    normalized.phase_degrees_ch2 = (uint16_t)(normalized.phase_degrees_ch2 % 360U);
    return normalized;
}

void app_dac_get_config(app_dac_config_t *config)
{
    if (config != 0) {
        *config = s_config;
    }
}

void app_dac_apply_config(const app_dac_config_t *config)
{
    if (config == 0) {
        return;
    }

    s_config = normalized_config(config);
    apply_output_config();
}

void app_dac_read_output(app_dac_output_t *output)
{
    uint16_t i;

    if (output == 0) {
        return;
    }

    output->config = s_config;
    output->waveform_sample_count = APP_DAC_TABLE_SIZE;
    for (i = 0U; i < APP_DAC_TABLE_SIZE; i++) {
        /* 使用 20ms（与仪表盘页一致）作为虚拟示波器时间轴计算预览波形，使频率变化在屏幕上可见 */
        uint32_t idx1 = ((uint32_t)i * s_config.frequency_hz * 2U) / 100U +
                        ((uint32_t)s_config.phase_degrees * APP_DAC_TABLE_SIZE) / 360U;
        output->waveform_ch1[i] = scale_sample(s_sine_fullscale[idx1 % APP_DAC_TABLE_SIZE], s_config.amplitude);

        if (s_config.mode == APP_DAC_MODE_DUAL) {
            uint32_t idx2 = ((uint32_t)i * s_config.frequency_hz_ch2 * 2U) / 100U +
                            ((uint32_t)s_config.phase_degrees_ch2 * APP_DAC_TABLE_SIZE) / 360U;
            output->waveform_ch2[i] = scale_sample(s_sine_fullscale[idx2 % APP_DAC_TABLE_SIZE], s_config.amplitude_ch2);
        } else {
            output->waveform_ch2[i] = APP_DAC_MID_CODE;
        }
    }
}
