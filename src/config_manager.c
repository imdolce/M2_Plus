#include "config_manager.h"
#include "amt49406.h"
#include <debug.h>

#if (AMT49406_CONFIG_SELECT == 0)

const char* const ACTIVE_CONFIG_NAME = CFG0_PROFILE_NAME;

const eeprom_entry_t ACTIVE_EEPROM_CONFIG[GOLDEN_CONFIG_COUNT] = {
    { 8,  CFG0_REG_8  },
    { 9,  CFG0_REG_9  },
    { 10, CFG0_REG_10 },
    { 11, CFG0_REG_11 },
    { 12, CFG0_REG_12 },
    { 13, CFG0_REG_13 },
    { 15, CFG0_REG_15 },
    { 16, CFG0_REG_16 },
    { 17, CFG0_REG_17 },
    { 18, CFG0_REG_18 },
    { 20, CFG0_REG_20 },
    { 21, CFG0_REG_21 },
    { 22, CFG0_REG_22 }
};

#else

const char* const ACTIVE_CONFIG_NAME = CFG1_PROFILE_NAME;

const eeprom_entry_t ACTIVE_EEPROM_CONFIG[GOLDEN_CONFIG_COUNT] = {
    { 8,  CFG1_REG_8  },
    { 9,  CFG1_REG_9  },
    { 10, CFG1_REG_10 },
    { 11, CFG1_REG_11 },
    { 12, CFG1_REG_12 },
    { 13, CFG1_REG_13 },
    { 15, CFG1_REG_15 },
    { 16, CFG1_REG_16 },
    { 17, CFG1_REG_17 },
    { 18, CFG1_REG_18 },
    { 20, CFG1_REG_20 },
    { 21, CFG1_REG_21 },
    { 22, CFG1_REG_22 }
};

#endif

config_status_t Config_VerifyEEPROM(void) {
    for (uint8_t i = 0; i < GOLDEN_CONFIG_COUNT; i++) {
        uint8_t addr = ACTIVE_EEPROM_CONFIG[i].addr;
        uint16_t expected_val = ACTIVE_EEPROM_CONFIG[i].value;
        uint16_t read_val = 0;

        uint8_t res = AMT49406_ReadReg(addr, &read_val);
        if (res != AMT49406_OK) {
            return CONFIG_STATUS_ERROR;
        }

        /* For Word 17: bit 9 is I2C_SPD_MODE (0x0200). If Word 17 was burned as 512
         * or 640 (demand bits [8:0] non-zero), verify that I2C_SPD_MODE (bit 9) matches */
        if (addr == 17) {
            if ((read_val & 0x0200) != (expected_val & 0x0200)) {
                return CONFIG_STATUS_MISMATCH;
            }
        } else {
            if (read_val != expected_val) {
                return CONFIG_STATUS_MISMATCH;
            }
        }
    }

    return CONFIG_STATUS_MATCH;
}

config_status_t Config_FlashEEPROM(void) {
    /* Sequentially program each word into AMT49406 EEPROM */
    for (uint8_t i = 0; i < GOLDEN_CONFIG_COUNT; i++) {
        uint8_t addr = ACTIVE_EEPROM_CONFIG[i].addr;
        uint16_t target_val = ACTIVE_EEPROM_CONFIG[i].value;

        uint8_t res = AMT49406_WriteEEPROM(addr, target_val);
        if (res != AMT49406_OK) {
            return CONFIG_STATUS_ERROR;
        }

        Delay_Ms(20);
    }

    /* Verify written data */
    return Config_VerifyEEPROM();
}
