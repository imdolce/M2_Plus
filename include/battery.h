#ifndef __BATTERY_H
#define __BATTERY_H

#include <ch32v00X.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Battery & ADC Specifications
 * 1S Li-Ion/LiPo battery (3.0V - 4.2V nominal).
 * MCU runs at 5.0V; PD6 senses battery directly (1:1 ratio, 0-5V ADC).
 * PA1 (IP2312 D1) & PA2 (IP2312 D2) direct inputs (1:1 ratio, 0-5V ADC).
 * ============================================================ */

/* Sag compensation factor: mV added per speed step (0 to 10) */
#define BATTERY_SAG_COMP_MV_PER_STEP     25

/* Voltage Thresholds for 1S Li-Ion (in mV) */
#define BATT_THRESH_4_BARS               3900   /* >= 3.90V: 4 Bars */
#define BATT_THRESH_3_BARS               3700   /* >= 3.70V: 3 Bars */
#define BATT_THRESH_2_BARS               3500   /* >= 3.50V: 2 Bars */
#define BATT_THRESH_1_BAR                3350   /* >= 3.35V: 1 Bar */
#define BATT_THRESH_LOW_WARN             3300   /* <  3.30V: Low Battery Warning */
#define BATT_THRESH_CUTOFF               3050   /* <  3.05V: Critical Cutoff */

/* IP2312 Charging detection threshold:
 * 1.50V threshold at 5.0V reference: (1.5 / 5.0) * 1023 = 307 counts */
#define IP2312_DETECT_THRESHOLD_ADC      307

void Battery_Init(void);
void Battery_Update(uint8_t speed_level);
uint16_t Battery_GetMillivolts(void);
uint8_t Battery_GetBars(void);
uint8_t Battery_IsLow(void);
uint8_t Battery_IsCutoff(void);
uint8_t Battery_IsCharging(void);
uint8_t Battery_IsFullCharge(void);
void Battery_ResetChargeDetect(void);

uint16_t Battery_GetRawPA1(void);
uint16_t Battery_GetRawPA2(void);
uint16_t Battery_GetRawPD6(void);

/* Setup PA1 as EXTI interrupt for charge-plug wakeup from sleep */
void Battery_Prepare_Sleep_EXTI(void);

#ifdef __cplusplus
}
#endif

#endif /* __BATTERY_H */
