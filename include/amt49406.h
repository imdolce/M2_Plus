#ifndef __AMT49406_H
#define __AMT49406_H

#include <ch32v00X.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* AMT49406 I2C Peripheral Address */
#define AMT49406_I2C_ADDR_7BIT          0x55
#define AMT49406_I2C_ADDR_WRITE         (AMT49406_I2C_ADDR_7BIT << 1)       /* 0xAA */
#define AMT49406_I2C_ADDR_READ          ((AMT49406_I2C_ADDR_7BIT << 1) | 1) /* 0xAB */

/* Error Codes */
#define AMT49406_OK                     0
#define AMT49406_ERR_TIMEOUT            1
#define AMT49406_ERR_NACK               2
#define AMT49406_ERR_BUS                3

/* Active RAM Shadow Configuration Registers (EEPROM Address + 64) */
#define AMT49406_SHADOW_OFFSET          64
#define AMT49406_REG_CFG_8              72   /* 8 + 64: Speed, Direction, PWM Mode */
#define AMT49406_REG_CFG_9              73   /* 9 + 64: Resistance, Acceleration */
#define AMT49406_REG_CFG_10             74   /* 10 + 64: Current, SPD Mode */
#define AMT49406_REG_CFG_11             75   /* 11 + 64: Startup Mode, Open Drive */
#define AMT49406_REG_CFG_12             76   /* 12 + 64: Observer PID P, Inductance */
#define AMT49406_REG_CFG_13             77   /* 13 + 64: Observer PID I, Delay Start */
#define AMT49406_REG_CFG_15             79   /* 15 + 64: Deadtime, Soft On/Off, Safe Brake */
#define AMT49406_REG_CFG_16             80   /* 16 + 64: OCP, First Cycle Speed, Buffers */
#define AMT49406_REG_CFG_17             81   /* 17 + 64: Active I2C Speed Demand */
#define AMT49406_REG_CFG_18             82   /* 18 + 64: IPD Current Threshold */
#define AMT49406_REG_CFG_20             84   /* 20 + 64: Active Rated Voltage & Rsense */
#define AMT49406_REG_CFG_21             85   /* 21 + 64: Standby, Slight Move */
#define AMT49406_REG_CFG_22             86   /* 22 + 64: Ratio, Brake Mode */

/* Readback Telemetry Registers */
#define AMT49406_REG_MOTOR_SPEED        120
#define AMT49406_REG_BUS_CURRENT        121
#define AMT49406_REG_QAXIS_CURRENT      122
#define AMT49406_REG_VBB                123
#define AMT49406_REG_TEMPERATURE        124
#define AMT49406_REG_CONTROL_DEMAND     125
#define AMT49406_REG_CONTROL_COMMAND    126
#define AMT49406_REG_OPERATION_STATE    127

/* EEPROM Control Registers */
#define AMT49406_REG_EEPROM_CTRL        161
#define AMT49406_REG_EEPROM_ADDR        162
#define AMT49406_REG_EEPROM_DATA_IN     163

/* Useful Bitmasks */
#define AMT49406_CFG15_SOFT_OFF         (1 << 6)
#define AMT49406_CFG15_SOFT_ON          (1 << 7)
#define AMT49406_CFG17_I2C_SPD_MODE     (1 << 9)
#define AMT49406_CFG17_SPD_DEMAND_MASK  (0x01FF)

/* Conversion Helpers */
#define AMT49406_RAW_TO_VBB_MV(raw)     (((uint32_t)(raw) * 1000) / 5)   /* Millivolts: raw / 5 V */
#define AMT49406_RAW_TO_TEMP_C(raw)     ((int16_t)(raw) - 53)            /* Celsius */
#define AMT49406_RAW_TO_SPEED_HZ_X10(raw) (((uint32_t)(raw) * 53) / 10) /* Speed in 0.1 Hz */

/* Telemetry Data Structure */
typedef struct {
    uint16_t raw_vbb;
    uint16_t raw_temperature;
    uint16_t raw_speed;
    uint16_t raw_bus_current;
    uint16_t raw_qaxis_current;
    uint16_t raw_control_demand;
    uint16_t raw_control_command;
    uint16_t raw_op_state;
    uint32_t vbb_mv;
    int16_t  temp_c;
    uint32_t speed_hz_x10;
} AMT49406_Telemetry_t;

/* Driver API */
void    AMT49406_GPIO_Init(void);
void    AMT49406_SetDir(uint8_t reverse);
void    AMT49406_SetBrake(uint8_t brake_en);
void    AMT49406_I2C_Init(void);
void    AMT49406_I2C_Bus_Recover(void);
uint8_t AMT49406_ReadReg(uint8_t regAddr, uint16_t *data);
uint8_t AMT49406_WriteReg(uint8_t regAddr, uint16_t data);
uint8_t AMT49406_Capture_Boot_Window(uint16_t timeout_ms, uint16_t *elapsed_ms);
uint8_t AMT49406_ReadTelemetry(AMT49406_Telemetry_t *telemetry);
uint8_t AMT49406_SetSpeed(uint16_t demand);
uint8_t AMT49406_WriteEEPROM(uint8_t eeAddr, uint16_t data);
uint8_t AMT49406_EraseEEPROM(uint8_t eeAddr);

#ifdef __cplusplus
}
#endif

#endif /* __AMT49406_H */
