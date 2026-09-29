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

    /* Ensure PA1 and PA2 are standard GPIO / ADC inputs.
     * In CH32V003 AFIO_PCFR1, bit 15 (PA12_RM) MUST be 0 for GPIO mode.
     * (Setting bit 15 to 1 puts PA1/PA2 into external crystal oscillator mode). */
    AFIO->PCFR1 &= ~((uint32_t)1 << 15);

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
    /* 1. Read PA1 (Channel 1, IP2312 D1) and PA2 (Channel 0, IP2312 D2).
     * Pins are kept permanently in GPIO_Mode_IPD (internal pull-down ~40k):
     * - When unplugged: D1 and D2 have no external pull-downs or LEDs, so the MCU's
     *   internal 40k pull-down holds the pins solidly at 0.0V (0 counts).
     * - When plugged in: IP2312 actively sources 3-10mA, easily driving the pin to
     *   ~5.0V (1023 counts / logic HIGH) against the 40k pull-down. */
    last_raw_pa1 = read_adc_channel(ADC_Channel_1);
    last_raw_pa2 = read_adc_channel(ADC_Channel_0);

    /* 2. Read raw ADC on PD6 (Channel 6, 1S Battery) LAST */
    last_raw_pd6 = read_adc_channel(ADC_Channel_6);

    /* Direct conversion: 0-1023 -> 0-5000 mV */
    uint32_t measured_mv = ((uint32_t)last_raw_pd6 * 5000) / 1023;

    /* Apply load sag compensation */
    uint32_t sag_comp = (uint32_t)speed_level * BATTERY_SAG_COMP_MV_PER_STEP;
    uint32_t estimated_resting_mv = measured_mv + sag_comp;

    /* EMA Filter: 7/8 previous + 1/8 new */
    filtered_mv = (uint16_t)(((uint32_t)filtered_mv * 7 + estimated_resting_mv) / 8);

    /* 3. Charging status evaluation:
     * D1 (PA1): HIGH (~5V) while charging, LOW (0V) when full or unplugged.
     * D2 (PA2): HIGH (~5V) when full; LOW (0V) when unplugged.
     * Checked with both ADC reading and digital input pin for absolute stability: */
    uint8_t pa1_high = (last_raw_pa1 >= IP2312_DETECT_THRESHOLD_ADC) || 
                       (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_1) == Bit_SET);
    uint8_t pa2_high = (last_raw_pa2 >= IP2312_DETECT_THRESHOLD_ADC) || 
                       (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_2) == Bit_SET);

    uint8_t raw_plugged = pa1_high || pa2_high;

    /* Integrator filter:
     * Fast latch on plug-in (2 hits = 1s), stable decay on unplug (3 misses = 1.5s). */
    if (raw_plugged) {
        if (chg_integrator < 3) chg_integrator++;
    } else {
        if (chg_integrator > 0) chg_integrator--;
    }

    if (chg_integrator >= 2) {
        is_charging = 1;
    } else if (chg_integrator == 0) {
        is_charging = 0;
    }

    /* Full charge detection:
     * D2 is HIGH AND D1 is LOW (IP2312 charging finished).
     * If D1 is still HIGH, charging is active and any D2 0.5Hz blinking is ignored! */
    if (is_charging && pa2_high && !pa1_high) {
        is_full_charge = 1;
    } else {
        is_full_charge = 0;
    }
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
