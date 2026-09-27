#ifndef __CONFIG_MANAGER_H
#define __CONFIG_MANAGER_H

#include <ch32v00X.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Configuration Profile Selector
 * Choose which dumped profile to compile into firmware:
 *   Set AMT49406_CONFIG_SELECT to:
 *     0 -> amt49406_config0.json (Rated Speed = 314, Current = 628, 12V)
 *     1 -> amt49406_config1.json (Rated Speed = 400, Current = 800, 12V)
 * ============================================================ */
#ifndef AMT49406_CONFIG_SELECT
#define AMT49406_CONFIG_SELECT   1   /* 0: Config0, 1: Config1 */
#endif

typedef enum {
    CONFIG_STATUS_MATCH = 0,     /* EEPROM matches active configuration values */
    CONFIG_STATUS_MISMATCH,      /* EEPROM does not match, needs programming */
    CONFIG_STATUS_ERROR          /* I2C or hardware communication fault */
} config_status_t;

typedef struct {
    uint8_t  addr;
    uint16_t value;
} eeprom_entry_t;

#define GOLDEN_CONFIG_COUNT  13

/* ============================================================
 * Profile 0 (amt49406_config0.json)
 * Motor: 12.0V, 7 PP, Rated Speed 314, Current 628, Observer P50 I30
 * ============================================================ */
#define CFG0_PROFILE_NAME       "Config 0 (Rated Speed 314, 12V)"
#define CFG0_REG_8              16698   /* Rated Speed raw = 314, open loop */
#define CFG0_REG_9              29040   /* Motor Resistance = 113, accel */
#define CFG0_REG_10             25204   /* Rated Current = 628, spd mode */
#define CFG0_REG_11             54400   /* Startup mode = 1, power ctrl */
#define CFG0_REG_12             1842    /* Observer P = 50 */
#define CFG0_REG_13             30      /* Observer I = 30 */
#define CFG0_REG_15             2432    /* Deadtime = 9 (400ns) */
#define CFG0_REG_16             10860   /* OCP = 4 */
#define CFG0_REG_17             640     /* I2C_SPD_MODE = 1 (bit 9) */
#define CFG0_REG_18             2816    /* IPD current threshold */
#define CFG0_REG_20             11836   /* Rated Voltage = 12.0V, Rsense = 12.4mOhm */
#define CFG0_REG_21             32800   /* Standby mode = 1 */
#define CFG0_REG_22             33374   /* Brake mode = 1, Ratio = 30 */

/* ============================================================
 * Profile 1 (amt49406_config1.json)
 * Motor: 12.0V, 7 PP, Rated Speed 400, Current 1000, Observer P50 I30
 * ============================================================ */
#define CFG1_PROFILE_NAME       "Config 1 (Rated Speed 400, 12V)"
#define CFG1_REG_8              16784   /* Rated Speed raw = 400, open loop */
#define CFG1_REG_9              29040   /* Motor Resistance = 113, accel */
#define CFG1_REG_10             25376   /* Rated Current raw = 800 (~2174 mA), spd mode */
#define CFG1_REG_11             54400   /* Startup mode = 1, power ctrl */
#define CFG1_REG_12             1842    /* Observer P = 50 */
#define CFG1_REG_13             30      /* Observer I = 30 */
#define CFG1_REG_15             2432    /* Deadtime = 9 (400ns) */
#define CFG1_REG_16             10860   /* OCP = 4 */
#define CFG1_REG_17             512     /* I2C_SPD_MODE = 1 (bit 9), demand 0 */
#define CFG1_REG_18             2816    /* IPD current threshold */
#define CFG1_REG_20             11836   /* Rated Voltage = 12.0V, Rsense = 12.4mOhm */
#define CFG1_REG_21             32800   /* Standby mode = 1 */
#define CFG1_REG_22             33374   /* Brake mode = 1, Ratio = 30 */

/* Active profile data references */
extern const eeprom_entry_t ACTIVE_EEPROM_CONFIG[GOLDEN_CONFIG_COUNT];
extern const char* const ACTIVE_CONFIG_NAME;

/* Verification and Flashing API */
config_status_t Config_VerifyEEPROM(void);
config_status_t Config_FlashEEPROM(void);

#ifdef __cplusplus
}
#endif

#endif /* __CONFIG_MANAGER_H */
