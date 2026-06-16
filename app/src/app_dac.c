#include "app_dac.h"

#include "stm32f10x_dac.h"
#include "stm32f10x_dma.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_tim.h"

#define APP_DAC_TIMER_CLOCK_HZ 72000000U
#define APP_DAC_DEFAULT_HZ 50U
#define APP_DAC_MIN_HZ 1U
#define APP_DAC_MAX_HZ 1000U
#define APP_DAC_DEFAULT_AMP 1500U
#define APP_DAC_MAX_AMP 2047U
#define APP_DAC_MID_CODE 2048U

// 一整周期 128 点的满幅正弦表，数值范围是 0..4095。
// 后面按用户设置的 amplitude 缩放，不在运行时计算 sin()。
static const uint16_t s_sine_fullscale[APP_DAC_WAVEFORM_SAMPLES] = {
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

// DAC 通道 1 的 DMA 循环播放波形缓冲区
static uint16_t s_dac_ch1[APP_DAC_WAVEFORM_SAMPLES];
// DAC 通道 2 的 DMA 循环播放波形缓冲区
static uint16_t s_dac_ch2[APP_DAC_WAVEFORM_SAMPLES];

// 当前 DAC 输出配置。外部一次写入整份配置，不用分别猜每个字段会不会重建
// 波形、DMA 或定时器。
static app_dac_config_t s_config = {
    APP_DAC_MODE_SINGLE,
    APP_DAC_DEFAULT_HZ,
    APP_DAC_DEFAULT_AMP,
    0U
};

/**
 * @brief 将 DAC 波形频率限制到可输出范围。
 */
static uint32_t clamp_frequency(uint32_t hz)
{
    // DAC 波形频率太高时，采样率也会太高，TIM6 可能算不出合适参数。
    if (hz < APP_DAC_MIN_HZ) {
        return APP_DAC_MIN_HZ;
    }
    if (hz > APP_DAC_MAX_HZ) {
        return APP_DAC_MAX_HZ;
    }
    return hz;
}

/**
 * @brief 将 DAC 波形幅度限制到安全码值范围。
 */
static uint16_t clamp_amplitude(uint16_t code)
{
    return code > APP_DAC_MAX_AMP ? APP_DAC_MAX_AMP : code;
}

/**
 * @brief 按目标幅度缩放一个满幅正弦样点。
 */
static uint16_t scale_sample(uint16_t raw, uint16_t amplitude)
{
    // raw 以 2048 为中心上下摆动。先转成正负偏移量，按 amplitude 缩放，
    // 再加回 2048，得到新的 DAC 码值。
    // 原始满幅样点相对中点电压的有符号偏移
    int32_t signed_sample = (int32_t)raw - (int32_t)APP_DAC_MID_CODE;
    // 按当前 amplitude 缩放后得到的 DAC 码值
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

/**
 * @brief 根据当前 DAC 配置重建两路 DMA 波形缓冲区。
 */
static void rebuild_buffers(void)
{
    // 波形表重建下标
    uint16_t i;
    // 通道 2 相位偏移对应的样点偏移量
    uint16_t phase_index =
        (uint16_t)(((uint32_t)s_config.phase_degrees * APP_DAC_WAVEFORM_SAMPLES) / 360U);

    // CH1 始终输出正弦波；CH2 在 DUAL 模式下输出带相位偏移的正弦波，
    // 在 SINGLE 模式下保持 2048，也就是 DAC 中点电压。
    for (i = 0U; i < APP_DAC_WAVEFORM_SAMPLES; i++) {
        s_dac_ch1[i] = scale_sample(s_sine_fullscale[i], s_config.amplitude);
        if (s_config.mode == APP_DAC_MODE_DUAL) {
            // 通道 2 读取满幅表时使用的相位偏移下标
            uint16_t j = (uint16_t)((i + phase_index) % APP_DAC_WAVEFORM_SAMPLES);
            s_dac_ch2[i] = scale_sample(s_sine_fullscale[j], s_config.amplitude);
        } else {
            s_dac_ch2[i] = APP_DAC_MID_CODE;
        }
    }
}

/**
 * @brief 根据目标采样率计算 TIM6 的 PSC 和 ARR。
 */
static void calc_timer_params(uint32_t sample_rate_hz, uint16_t *psc, uint16_t *arr)
{
    // TIM6 每次 Update 触发 DMA 写一个 DAC 采样点。
    // 所以采样率 = 波形频率 * 每周期采样点数。
    // 目标采样周期需要的定时器 tick 数
    uint32_t target_ticks = (APP_DAC_TIMER_CLOCK_HZ + (sample_rate_hz / 2U)) / sample_rate_hz;
    // TIM6 预分频寄存器值，写入时对应 PSC
    uint32_t prescaler = (target_ticks + 65535U) / 65536U;
    // TIM6 一个采样周期内的计数点数
    uint32_t period;

    if (prescaler > 0U) {
        prescaler--;
    }
    if (prescaler > 65535U) {
        prescaler = 65535U;
    }

    period = ((APP_DAC_TIMER_CLOCK_HZ / (prescaler + 1U)) + (sample_rate_hz / 2U)) / sample_rate_hz;
    if (period < 2U) {
        period = 2U;
    }
    if (period > 65536U) {
        period = 65536U;
    }

    *psc = (uint16_t)prescaler;
    *arr = (uint16_t)(period - 1U);
}

/**
 * @brief 配置 DAC 两个通道对应的循环 DMA。
 */
static void configure_dma(void)
{
    // DAC DMA 通道通用配置结构体
    DMA_InitTypeDef dma;

    // DMA2_Channel3 对应 DAC 通道 1。Circular 模式会反复播放 s_dac_ch1，
    // CPU 不需要在主循环里一个点一个点写 DAC。
    DMA_DeInit(DMA2_Channel3);
    DMA_StructInit(&dma);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&DAC->DHR12R1;
    dma.DMA_MemoryBaseAddr = (uint32_t)s_dac_ch1;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize = APP_DAC_WAVEFORM_SAMPLES;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode = DMA_Mode_Circular;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA2_Channel3, &dma);

    // DAC 通道 2 用 DMA2_Channel4，配置基本相同，只换寄存器和波形表。
    DMA_DeInit(DMA2_Channel4);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&DAC->DHR12R2;
    dma.DMA_MemoryBaseAddr = (uint32_t)s_dac_ch2;
    DMA_Init(DMA2_Channel4, &dma);
}

/**
 * @brief 将当前 DAC 配置应用到波形表、DMA 和 TIM6。
 */
static void apply_output_config(void)
{
    // TIM6 预分频寄存器值
    uint16_t psc;
    // TIM6 自动重装载寄存器值
    uint16_t arr;
    // DAC 每秒输出的采样点数
    uint32_t sample_rate_hz = s_config.frequency_hz * APP_DAC_WAVEFORM_SAMPLES;

    // 改波形参数前先停 TIM6 和 DMA，防止 DMA 一边读表，一边表被改写。
    TIM_Cmd(TIM6, DISABLE);
    DMA_Cmd(DMA2_Channel3, DISABLE);
    DMA_Cmd(DMA2_Channel4, DISABLE);

    // 先重建两路波形表，再设置 DMA 和 TIM6 触发频率。
    rebuild_buffers();
    configure_dma();
    calc_timer_params(sample_rate_hz, &psc, &arr);

    TIM_PrescalerConfig(TIM6, psc, TIM_PSCReloadMode_Immediate);
    TIM_SetAutoreload(TIM6, arr);
    TIM_GenerateEvent(TIM6, TIM_EventSource_Update);

    DMA_Cmd(DMA2_Channel3, ENABLE);
    DMA_Cmd(DMA2_Channel4, ENABLE);
    TIM_Cmd(TIM6, ENABLE);
}

/**
 * @brief 初始化 PA4/PA5 DAC 双通道波形输出。
 */
void app_dac_init(void)
{
    // PA4/PA5 DAC 输出引脚配置
    GPIO_InitTypeDef gpio;
    // DAC 通道公共配置
    DAC_InitTypeDef dac;
    // TIM6 基础计数参数配置
    TIM_TimeBaseInitTypeDef time_base;

    // PA4/PA5 是 STM32F103 的 DAC_OUT1/DAC_OUT2，引脚必须配置成模拟输入模式。
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC | RCC_APB1Periph_TIM6, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5;
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &gpio);

    TIM_TimeBaseStructInit(&time_base);
    time_base.TIM_CounterMode = TIM_CounterMode_Up;
    time_base.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM6, &time_base);
    // TIM6 Update 事件作为 DAC 触发源：定时器每到一个采样周期，
    // DAC 就从 DMA 收到下一个采样点。
    TIM_SelectOutputTrigger(TIM6, TIM_TRGOSource_Update);

    DAC_StructInit(&dac);
    dac.DAC_Trigger = DAC_Trigger_T6_TRGO;
    dac.DAC_WaveGeneration = DAC_WaveGeneration_None;
    dac.DAC_LFSRUnmask_TriangleAmplitude = DAC_LFSRUnmask_Bit0;
    dac.DAC_OutputBuffer = DAC_OutputBuffer_Enable;
    DAC_Init(DAC_Channel_1, &dac);
    DAC_Init(DAC_Channel_2, &dac);

    DAC_Cmd(DAC_Channel_1, ENABLE);
    DAC_Cmd(DAC_Channel_2, ENABLE);
    // 打开 DAC 的 DMA 请求，后续 TIM6 触发时 DMA 才会自动搬运数据。
    DAC_DMACmd(DAC_Channel_1, ENABLE);
    DAC_DMACmd(DAC_Channel_2, ENABLE);

    apply_output_config();
}

/**
 * @brief 归一化外部传入的 DAC 配置。
 */
static app_dac_config_t normalized_config(const app_dac_config_t *config)
{
    // 经过默认值补齐和范围限制后的配置
    app_dac_config_t normalized = s_config;

    if (config != 0) {
        normalized = *config;
    }

    // 外部传入的配置先整理一遍：非法 mode 回到 SINGLE，数值限制到本模块
    // 能处理的范围。
    normalized.mode =
        normalized.mode == APP_DAC_MODE_DUAL ? APP_DAC_MODE_DUAL : APP_DAC_MODE_SINGLE;
    normalized.frequency_hz = clamp_frequency(normalized.frequency_hz);
    normalized.amplitude = clamp_amplitude(normalized.amplitude);
    normalized.phase_degrees = (uint16_t)(normalized.phase_degrees % 360U);
    return normalized;
}

/**
 * @brief 读取当前 DAC 输出配置。
 */
void app_dac_get_config(app_dac_config_t *config)
{
    if (config != 0) {
        *config = s_config;
    }
}

/**
 * @brief 写入并应用新的 DAC 输出配置。
 */
void app_dac_apply_config(const app_dac_config_t *config)
{
    if (config == 0) {
        return;
    }

    s_config = normalized_config(config);
    apply_output_config();
}

/**
 * @brief 读取 DAC 当前配置和波形预览数据。
 */
void app_dac_read_output(app_dac_output_t *output)
{
    // 波形表复制下标
    uint16_t i;

    if (output == 0) {
        return;
    }

    // 这里复制一份波形表给调用者。调用者改不到 DMA 正在播放的
    // s_dac_ch1/s_dac_ch2。
    output->config = s_config;
    output->waveform_sample_count = APP_DAC_WAVEFORM_SAMPLES;
    for (i = 0U; i < APP_DAC_WAVEFORM_SAMPLES; i++) {
        output->waveform_ch1[i] = s_dac_ch1[i];
        output->waveform_ch2[i] = s_dac_ch2[i];
    }
}
