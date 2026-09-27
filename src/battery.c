#include "battery.h"
#include <debug.h>

static uint16_t filtered_mv = 3850;
static uint8_t  current_bars = 3;
static uint8_t  is_charging = 0;
static uint8_t  is_full_charge = 0;

static uint16_t last_raw_pa1 = 0;
static uint16_t last_raw_pa2 = 0;
static uint16_t last_raw_pd6 = 0;
static uint8_t  chg_integrator = 0;

static uint16_t read_adc_channel(uint8_t channel) {
    ADC_RegularChannelConfig(ADC1, channel, 1, ADC_SampleTime_241Cycles);
    uint32_t sum = 0;
    for (uint8_t i = 0; i < 4; i++) {
        ADC_SoftwareStartConvCmd(ADC1, ENABLE);
        uint32_t timeout = 50000;
        while (!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) && --timeout);
        sum += ADC_GetConversionValue(ADC1);
    }
    return (uint16_t)(sum / 4);
}

void Battery_Init(void) {
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    ADC_InitTypeDef  ADC_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOD | RCC_APB2Periph_ADC1 | RCC_APB2Periph_AFIO, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div8);

    /* Remap PA1 and PA2 from OSC_IN/OSC_OUT to standard GPIO / ADC inputs */
    GPIO_PinRemapConfig(GPIO_Remap_PA1_2, ENABLE);

    /* PA1 (ADC_Channel_1): Charge in detect
     * PA2 (ADC_Channel_0): Full charge detect
     * Default to IPD (internal pull-down) so pins read ~0V when IP2312 is
     * unpowered (no USB). Battery_Update() briefly switches to AIN for reads. */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* PD6 (ADC_Channel_6): Direct 1S Battery voltage sense */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    /* Configure ADC1 */
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &ADC_InitStructure);

    ADC_Cmd(ADC1, ENABLE);

    /* Calibration */
    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1));

    /* Initialize battery voltage filter directly from PD6 reading */
    last_raw_pd6 = read_adc_channel(ADC_Channel_6);
    filtered_mv = (uint16_t)(((uint32_t)last_raw_pd6 * 5000) / 1023);
    chg_integrator = 0;
    is_charging = 0;
    is_full_charge = 0;

    /* Initialize initial bar count based on stabilized voltage */
    if (filtered_mv >= BATT_THRESH_4_BARS) current_bars = 4;
    else if (filtered_mv >= BATT_THRESH_3_BARS) current_bars = 3;
    else if (filtered_mv >= BATT_THRESH_2_BARS) current_bars = 2;
    else if (filtered_mv >= BATT_THRESH_1_BAR)  current_bars = 1;
    else current_bars = 0;
}

void Battery_Update(uint8_t speed_level) {
    /* 1. Read raw ADC on PD6 (Channel 6) */
    last_raw_pd6 = read_adc_channel(ADC_Channel_6);

    /* Direct conversion: 0-1023 -> 0-5000 mV */
    uint32_t measured_mv = ((uint32_t)last_raw_pd6 * 5000) / 1023;

    /* Apply load sag compensation */
    uint32_t sag_comp = (uint32_t)speed_level * BATTERY_SAG_COMP_MV_PER_STEP;
    uint32_t estimated_resting_mv = measured_mv + sag_comp;

    /* EMA Filter: 7/8 previous + 1/8 new */
    filtered_mv = (uint16_t)(((uint32_t)filtered_mv * 7 + estimated_resting_mv) / 8);

    /* 2. Read PA1 (Channel 1, IP2312 D1) and PA2 (Channel 0, IP2312 D2)
     * Pins default to IPD (pull-down). Switch to AIN for ADC read,
     * do a dummy read to flush S&H cap from PD6 voltage, then read for real,
     * then switch back to IPD to eliminate floating noise. */
    {
        GPIO_InitTypeDef gpio = {0};
        gpio.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_2;
        gpio.GPIO_Mode = GPIO_Mode_AIN;
        GPIO_Init(GPIOA, &gpio);

        (void)read_adc_channel(ADC_Channel_1); /* Dummy read: flush S&H cap from PD6 */
        last_raw_pa1 = read_adc_channel(ADC_Channel_1);
        last_raw_pa2 = read_adc_channel(ADC_Channel_0);

        gpio.GPIO_Mode = GPIO_Mode_IPD;
        GPIO_Init(GPIOA, &gpio);
    }

    /* Plugged-in detection: Either D1 >= 1.5V (charging) OR D2 >= 1.5V (full)
     * 2-sample integrator debounce prevents false triggers from transient noise.
     * At 500ms polling rate: ~1s to confirm plug-in, ~1s to confirm unplug. */
    uint8_t raw_plugged = (last_raw_pa1 >= IP2312_DETECT_THRESHOLD_ADC) || 
                          (last_raw_pa2 >= IP2312_DETECT_THRESHOLD_ADC);

    if (raw_plugged) {
        if (chg_integrator < 2) chg_integrator++;
    } else {
        if (chg_integrator > 0) chg_integrator--;
    }

    if (chg_integrator >= 2) {
        is_charging = 1;
    } else if (chg_integrator == 0) {
        is_charging = 0;
    }

    /* Full charge detection: D2 >= 1.5V (PA2) while charger is confirmed plugged in */
    is_full_charge = is_charging && (last_raw_pa2 >= IP2312_DETECT_THRESHOLD_ADC);
}

void Battery_ResetChargeDetect(void) {
    chg_integrator = 0;
    is_charging = 0;
    is_full_charge = 0;
}

uint16_t Battery_GetMillivolts(void) {
    return filtered_mv;
}

uint8_t Battery_GetBars(void) {
    /* 70 mV hysteresis (+30mV up, -40mV down) to eliminate bar oscillation */
    switch (current_bars) {
        case 4:
            if (filtered_mv < (BATT_THRESH_4_BARS - 40)) current_bars = 3;
            break;
        case 3:
            if (filtered_mv >= (BATT_THRESH_4_BARS + 30)) current_bars = 4;
            else if (filtered_mv < (BATT_THRESH_3_BARS - 40)) current_bars = 2;
            break;
        case 2:
            if (filtered_mv >= (BATT_THRESH_3_BARS + 30)) current_bars = 3;
            else if (filtered_mv < (BATT_THRESH_2_BARS - 40)) current_bars = 1;
            break;
        case 1:
            if (filtered_mv >= (BATT_THRESH_2_BARS + 30)) current_bars = 2;
            else if (filtered_mv < (BATT_THRESH_1_BAR - 40)) current_bars = 0;
            break;
        case 0:
        default:
            if (filtered_mv >= (BATT_THRESH_1_BAR + 30)) current_bars = 1;
            break;
    }
    return current_bars;
}

uint8_t Battery_IsLow(void) {
    return (filtered_mv < BATT_THRESH_LOW_WARN);
}

uint8_t Battery_IsCutoff(void) {
    return (filtered_mv < BATT_THRESH_CUTOFF);
}

uint8_t Battery_IsCharging(void) {
    return is_charging;
}

uint8_t Battery_IsFullCharge(void) {
    return is_full_charge;
}

uint16_t Battery_GetRawPA1(void) {
    return last_raw_pa1;
}

uint16_t Battery_GetRawPA2(void) {
    return last_raw_pa2;
}

uint16_t Battery_GetRawPD6(void) {
    return last_raw_pd6;
}

void Battery_Prepare_Sleep_EXTI(void) {
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    EXTI_InitTypeDef EXTI_InitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);

    /* PA1: Charge in detect pin - Pull-Down so it does not float when unplugged */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource1);

    EXTI_InitStructure.EXTI_Line = EXTI_Line1;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = EXTI7_0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}
