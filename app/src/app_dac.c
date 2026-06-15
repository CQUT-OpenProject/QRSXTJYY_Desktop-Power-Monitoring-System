#include "app_dac.h"

#include "stm32f10x_dac.h"
#include "stm32f10x_dma.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_tim.h"

#define APP_DAC_TIMER_CLOCK_HZ 72000000U
#define APP_DAC_TABLE_SIZE 128U
#define APP_DAC_DEFAULT_HZ 100U
#define APP_DAC_MIN_HZ 1U
#define APP_DAC_MAX_HZ 1000U
#define APP_DAC_DEFAULT_AMP 1500U
#define APP_DAC_MAX_AMP 2047U
#define APP_DAC_MID_CODE 2048U

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
static app_dac_mode_t s_mode = APP_DAC_MODE_SINGLE;
static uint32_t s_frequency_hz = APP_DAC_DEFAULT_HZ;
static uint16_t s_amplitude = APP_DAC_DEFAULT_AMP;
static uint16_t s_phase_degrees;

static uint32_t clamp_frequency(uint32_t hz)
{
    if (hz < APP_DAC_MIN_HZ) {
        return APP_DAC_MIN_HZ;
    }
    if (hz > APP_DAC_MAX_HZ) {
        return APP_DAC_MAX_HZ;
    }
    return hz;
}

static uint16_t clamp_amplitude(uint16_t code)
{
    return code > APP_DAC_MAX_AMP ? APP_DAC_MAX_AMP : code;
}

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

static void rebuild_buffers(void)
{
    uint16_t i;
    uint16_t phase_index = (uint16_t)(((uint32_t)s_phase_degrees * APP_DAC_TABLE_SIZE) / 360U);

    for (i = 0U; i < APP_DAC_TABLE_SIZE; i++) {
        s_dac_ch1[i] = scale_sample(s_sine_fullscale[i], s_amplitude);
        if (s_mode == APP_DAC_MODE_DUAL) {
            uint16_t j = (uint16_t)((i + phase_index) % APP_DAC_TABLE_SIZE);
            s_dac_ch2[i] = scale_sample(s_sine_fullscale[j], s_amplitude);
        } else {
            s_dac_ch2[i] = APP_DAC_MID_CODE;
        }
    }
}

static void calc_timer_params(uint32_t sample_rate_hz, uint16_t *psc, uint16_t *arr)
{
    uint32_t target_ticks = (APP_DAC_TIMER_CLOCK_HZ + (sample_rate_hz / 2U)) / sample_rate_hz;
    uint32_t prescaler = (target_ticks + 65535U) / 65536U;
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

static void configure_dma(void)
{
    DMA_InitTypeDef dma;

    DMA_DeInit(DMA2_Channel3);
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

    DMA_DeInit(DMA2_Channel4);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&DAC->DHR12R2;
    dma.DMA_MemoryBaseAddr = (uint32_t)s_dac_ch2;
    DMA_Init(DMA2_Channel4, &dma);
}

static void apply_output_config(void)
{
    uint16_t psc;
    uint16_t arr;
    uint32_t sample_rate_hz = s_frequency_hz * APP_DAC_TABLE_SIZE;

    TIM_Cmd(TIM6, DISABLE);
    DMA_Cmd(DMA2_Channel3, DISABLE);
    DMA_Cmd(DMA2_Channel4, DISABLE);

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

void app_dac_init(void)
{
    GPIO_InitTypeDef gpio;
    DAC_InitTypeDef dac;
    TIM_TimeBaseInitTypeDef time_base;

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
    DAC_DMACmd(DAC_Channel_1, ENABLE);
    DAC_DMACmd(DAC_Channel_2, ENABLE);

    apply_output_config();
}

void app_dac_set_mode(app_dac_mode_t mode)
{
    s_mode = mode == APP_DAC_MODE_DUAL ? APP_DAC_MODE_DUAL : APP_DAC_MODE_SINGLE;
    apply_output_config();
}

void app_dac_set_frequency(uint32_t hz)
{
    s_frequency_hz = clamp_frequency(hz);
    apply_output_config();
}

void app_dac_set_amplitude(uint16_t code)
{
    s_amplitude = clamp_amplitude(code);
    apply_output_config();
}

void app_dac_set_phase(uint16_t degrees)
{
    s_phase_degrees = (uint16_t)(degrees % 360U);
    apply_output_config();
}

app_dac_mode_t app_dac_get_mode(void)
{
    return s_mode;
}

uint32_t app_dac_get_frequency(void)
{
    return s_frequency_hz;
}

uint16_t app_dac_get_amplitude(void)
{
    return s_amplitude;
}

uint16_t app_dac_get_phase(void)
{
    return s_phase_degrees;
}

uint16_t app_dac_get_sample(uint8_t channel, uint16_t index)
{
    index = (uint16_t)(index % APP_DAC_TABLE_SIZE);
    if (channel == 2U) {
        return s_dac_ch2[index];
    }
    return s_dac_ch1[index];
}

uint16_t app_dac_get_table_size(void)
{
    return APP_DAC_TABLE_SIZE;
}
