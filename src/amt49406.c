#include "amt49406.h"
#include <debug.h>

#define I2C_TIMEOUT_CYCLES  30000

void AMT49406_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

    /* PC3: DIR pin (Output Push-Pull) */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
    GPIO_ResetBits(GPIOC, GPIO_Pin_3); /* 0 = Forward */

    /* PC5: BRAKE pin (Output Push-Pull) */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
    GPIO_ResetBits(GPIOC, GPIO_Pin_5); /* 0 = Run / Coast */
}

void AMT49406_SetDir(uint8_t reverse) {
    if (reverse) {
        GPIO_SetBits(GPIOC, GPIO_Pin_3);
    } else {
        GPIO_ResetBits(GPIOC, GPIO_Pin_3);
    }
}

void AMT49406_SetBrake(uint8_t brake_en) {
    if (brake_en) {
        GPIO_SetBits(GPIOC, GPIO_Pin_5);
    } else {
        GPIO_ResetBits(GPIOC, GPIO_Pin_5);
    }
}

void AMT49406_I2C_Init(void) {
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    I2C_InitTypeDef  I2C_InitStructure = {0};

    /* Enable GPIOC and I2C1 peripheral clocks */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

    /* PC1 = SDA, PC2 = SCL: Alternate Function Open-Drain */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    /* Reset I2C1 state machine */
    I2C_SoftwareResetCmd(I2C1, ENABLE);
    I2C_SoftwareResetCmd(I2C1, DISABLE);

    /* Initialize I2C1 Master Mode at 100 kHz */
    I2C_InitStructure.I2C_ClockSpeed = 100000;
    I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2;
    I2C_InitStructure.I2C_OwnAddress1 = 0x02;
    I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(I2C1, &I2C_InitStructure);

    I2C_Cmd(I2C1, ENABLE);
    I2C_AcknowledgeConfig(I2C1, ENABLE);
}

void AMT49406_I2C_Bus_Recover(void) {
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    /* Disable I2C1 peripheral and reset */
    I2C_Cmd(I2C1, DISABLE);
    I2C_SoftwareResetCmd(I2C1, ENABLE);
    I2C_SoftwareResetCmd(I2C1, DISABLE);

    /* Configure PC1 (SDA) and PC2 (SCL) as GPIO Open-Drain output */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    GPIO_SetBits(GPIOC, GPIO_Pin_1 | GPIO_Pin_2);
    Delay_Us(10);

    /* Clock SCL 9 times to release any held SDA line */
    for (uint8_t i = 0; i < 9; i++) {
        GPIO_ResetBits(GPIOC, GPIO_Pin_2);
        Delay_Us(5);
        GPIO_SetBits(GPIOC, GPIO_Pin_2);
        Delay_Us(5);
    }

    /* Generate manual STOP condition: SDA low -> SCL high -> SDA high */
    GPIO_ResetBits(GPIOC, GPIO_Pin_1);
    Delay_Us(5);
    GPIO_SetBits(GPIOC, GPIO_Pin_2);
    Delay_Us(5);
    GPIO_SetBits(GPIOC, GPIO_Pin_1);
    Delay_Us(10);

    /* Restore Alternate Function Open-Drain mode */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    /* Re-enable I2C1 peripheral */
    I2C_Cmd(I2C1, ENABLE);
    I2C_AcknowledgeConfig(I2C1, ENABLE);
}

uint8_t AMT49406_ReadReg(uint8_t regAddr, uint16_t *data) {
    uint32_t to = I2C_TIMEOUT_CYCLES;

    /* Check if bus is stuck busy */
    while (I2C_GetFlagStatus(I2C1, I2C_FLAG_BUSY)) {
        if (--to == 0) {
            AMT49406_I2C_Bus_Recover();
            return AMT49406_ERR_BUS;
        }
    }

    /* 1. Generate START */
    I2C_GenerateSTART(I2C1, ENABLE);
    to = I2C_TIMEOUT_CYCLES;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_MODE_SELECT)) {
        if (--to == 0) {
            AMT49406_I2C_Bus_Recover();
            return AMT49406_ERR_TIMEOUT;
        }
    }

    /* 2. Send 7-bit Address + Write (0xAA) */
    I2C_Send7bitAddress(I2C1, AMT49406_I2C_ADDR_WRITE, I2C_Direction_Transmitter);
    to = I2C_TIMEOUT_CYCLES;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) {
        if (I2C_GetFlagStatus(I2C1, I2C_FLAG_AF)) {
            I2C_ClearFlag(I2C1, I2C_FLAG_AF);
            I2C_GenerateSTOP(I2C1, ENABLE);
            return AMT49406_ERR_NACK;
        }
        if (--to == 0) {
            AMT49406_I2C_Bus_Recover();
            return AMT49406_ERR_TIMEOUT;
        }
    }

    /* 3. Send Register Address */
    I2C_SendData(I2C1, regAddr);
    to = I2C_TIMEOUT_CYCLES;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
        if (I2C_GetFlagStatus(I2C1, I2C_FLAG_AF)) {
            I2C_ClearFlag(I2C1, I2C_FLAG_AF);
            I2C_GenerateSTOP(I2C1, ENABLE);
            return AMT49406_ERR_NACK;
        }
        if (--to == 0) {
            AMT49406_I2C_Bus_Recover();
            return AMT49406_ERR_TIMEOUT;
        }
    }

    /* 4. Repeated START */
    I2C_GenerateSTART(I2C1, ENABLE);
    to = I2C_TIMEOUT_CYCLES;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_MODE_SELECT)) {
        if (--to == 0) {
            AMT49406_I2C_Bus_Recover();
            return AMT49406_ERR_TIMEOUT;
        }
    }

    /* 5. Send 7-bit Address + Read (0xAB) */
    I2C_Send7bitAddress(I2C1, AMT49406_I2C_ADDR_READ, I2C_Direction_Receiver);
    to = I2C_TIMEOUT_CYCLES;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)) {
        if (I2C_GetFlagStatus(I2C1, I2C_FLAG_AF)) {
            I2C_ClearFlag(I2C1, I2C_FLAG_AF);
            I2C_GenerateSTOP(I2C1, ENABLE);
            return AMT49406_ERR_NACK;
        }
        if (--to == 0) {
            AMT49406_I2C_Bus_Recover();
            return AMT49406_ERR_TIMEOUT;
        }
    }

    /* 6. Read Byte 1 (MSB) with ACK */
    I2C_AcknowledgeConfig(I2C1, ENABLE);
    to = I2C_TIMEOUT_CYCLES;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_RECEIVED)) {
        if (--to == 0) {
            AMT49406_I2C_Bus_Recover();
            return AMT49406_ERR_TIMEOUT;
        }
    }
    uint8_t msb = I2C_ReceiveData(I2C1);

    /* 7. Read Byte 2 (LSB) with NACK and generate STOP */
    I2C_AcknowledgeConfig(I2C1, DISABLE);
    I2C_GenerateSTOP(I2C1, ENABLE);

    to = I2C_TIMEOUT_CYCLES;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_RECEIVED)) {
        if (--to == 0) {
            AMT49406_I2C_Bus_Recover();
            return AMT49406_ERR_TIMEOUT;
        }
    }
    uint8_t lsb = I2C_ReceiveData(I2C1);

    /* Re-enable ACK for future transactions */
    I2C_AcknowledgeConfig(I2C1, ENABLE);

    if (data != 0) {
        *data = ((uint16_t)msb << 8) | lsb;
    }
    return AMT49406_OK;
}

uint8_t AMT49406_WriteReg(uint8_t regAddr, uint16_t data) {
    uint32_t to = I2C_TIMEOUT_CYCLES;

    while (I2C_GetFlagStatus(I2C1, I2C_FLAG_BUSY)) {
        if (--to == 0) {
            AMT49406_I2C_Bus_Recover();
            return AMT49406_ERR_BUS;
        }
    }

    /* 1. Generate START */
    I2C_GenerateSTART(I2C1, ENABLE);
    to = I2C_TIMEOUT_CYCLES;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_MODE_SELECT)) {
        if (--to == 0) {
            AMT49406_I2C_Bus_Recover();
            return AMT49406_ERR_TIMEOUT;
        }
    }

    /* 2. Send 7-bit Address + Write (0xAA) */
    I2C_Send7bitAddress(I2C1, AMT49406_I2C_ADDR_WRITE, I2C_Direction_Transmitter);
    to = I2C_TIMEOUT_CYCLES;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) {
        if (I2C_GetFlagStatus(I2C1, I2C_FLAG_AF)) {
            I2C_ClearFlag(I2C1, I2C_FLAG_AF);
            I2C_GenerateSTOP(I2C1, ENABLE);
            return AMT49406_ERR_NACK;
        }
        if (--to == 0) {
            AMT49406_I2C_Bus_Recover();
            return AMT49406_ERR_TIMEOUT;
        }
    }

    /* 3. Send Register Address */
    I2C_SendData(I2C1, regAddr);
    to = I2C_TIMEOUT_CYCLES;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
        if (--to == 0) {
            AMT49406_I2C_Bus_Recover();
            return AMT49406_ERR_TIMEOUT;
        }
    }

    /* 4. Send Data MSB */
    uint8_t msb = (uint8_t)(data >> 8);
    I2C_SendData(I2C1, msb);
    to = I2C_TIMEOUT_CYCLES;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
        if (--to == 0) {
            AMT49406_I2C_Bus_Recover();
            return AMT49406_ERR_TIMEOUT;
        }
    }

    /* 5. Send Data LSB */
    uint8_t lsb = (uint8_t)(data & 0xFF);
    I2C_SendData(I2C1, lsb);
    to = I2C_TIMEOUT_CYCLES;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
        if (--to == 0) {
            AMT49406_I2C_Bus_Recover();
            return AMT49406_ERR_TIMEOUT;
        }
    }

    I2C_GenerateSTOP(I2C1, ENABLE);
    return AMT49406_OK;
}

uint8_t AMT49406_Capture_Boot_Window(uint16_t timeout_ms, uint16_t *elapsed_ms) {
    uint16_t dummy = 0;
    uint32_t start_time = 0;
    uint32_t attempts = 0;

    for (start_time = 0; start_time < timeout_ms; start_time += 2) {
        attempts++;
        /* Probe Register 123 (V_BB) */
        uint8_t status = AMT49406_ReadReg(AMT49406_REG_VBB, &dummy);
        if (status == AMT49406_OK) {
            if (elapsed_ms != 0) {
                *elapsed_ms = (uint16_t)start_time;
            }
            return AMT49406_OK;
        }

        /* Wait ~2 ms between attempts to allow power/bus settling */
        Delay_Ms(2);
    }

    if (elapsed_ms != 0) {
        *elapsed_ms = timeout_ms;
    }
    return AMT49406_ERR_TIMEOUT;
}

uint8_t AMT49406_ReadTelemetry(AMT49406_Telemetry_t *telemetry) {
    if (telemetry == 0) return AMT49406_ERR_NACK;

    uint8_t res;
    res = AMT49406_ReadReg(AMT49406_REG_VBB, &telemetry->raw_vbb);
    if (res != AMT49406_OK) return res;

    res = AMT49406_ReadReg(AMT49406_REG_TEMPERATURE, &telemetry->raw_temperature);
    if (res != AMT49406_OK) return res;

    res = AMT49406_ReadReg(AMT49406_REG_MOTOR_SPEED, &telemetry->raw_speed);
    if (res != AMT49406_OK) return res;

    res = AMT49406_ReadReg(AMT49406_REG_BUS_CURRENT, &telemetry->raw_bus_current);
    if (res != AMT49406_OK) return res;

    res = AMT49406_ReadReg(AMT49406_REG_QAXIS_CURRENT, &telemetry->raw_qaxis_current);
    if (res != AMT49406_OK) return res;

    res = AMT49406_ReadReg(AMT49406_REG_CONTROL_DEMAND, &telemetry->raw_control_demand);
    if (res != AMT49406_OK) return res;

    res = AMT49406_ReadReg(AMT49406_REG_CONTROL_COMMAND, &telemetry->raw_control_command);
    if (res != AMT49406_OK) return res;

    res = AMT49406_ReadReg(AMT49406_REG_OPERATION_STATE, &telemetry->raw_op_state);
    if (res != AMT49406_OK) return res;

    /* Compute engineering units */
    telemetry->vbb_mv = AMT49406_RAW_TO_VBB_MV(telemetry->raw_vbb);
    telemetry->temp_c = AMT49406_RAW_TO_TEMP_C(telemetry->raw_temperature);
    telemetry->speed_hz_x10 = AMT49406_RAW_TO_SPEED_HZ_X10(telemetry->raw_speed);

    return AMT49406_OK;
}

uint8_t AMT49406_SetSpeed(uint16_t demand) {
    if (demand > 511) {
        demand = 511;
    }
    uint16_t reg17 = AMT49406_CFG17_I2C_SPD_MODE | (demand & AMT49406_CFG17_SPD_DEMAND_MASK);
    /* Write to active shadow register 81 (17 + 64) and mirror register 17 */
    AMT49406_WriteReg(17, reg17);
    return AMT49406_WriteReg(AMT49406_REG_CFG_17, reg17);
}

uint8_t AMT49406_WriteEEPROM(uint8_t eeAddr, uint16_t data) {
    uint8_t status;

    /* 1. Erase sequence */
    status = AMT49406_WriteReg(AMT49406_REG_EEPROM_ADDR, eeAddr & 0x1F);
    if (status != AMT49406_OK) return status;

    status = AMT49406_WriteReg(AMT49406_REG_EEPROM_DATA_IN, 0x0000);
    if (status != AMT49406_OK) return status;

    /* ER=1, EN=1 -> 0x0003 */
    status = AMT49406_WriteReg(AMT49406_REG_EEPROM_CTRL, 0x0003);
    if (status != AMT49406_OK) return status;

    Delay_Ms(16); /* High voltage erase pulse */

    /* 2. Write sequence */
    status = AMT49406_WriteReg(AMT49406_REG_EEPROM_ADDR, eeAddr & 0x1F);
    if (status != AMT49406_OK) return status;

    status = AMT49406_WriteReg(AMT49406_REG_EEPROM_DATA_IN, data);
    if (status != AMT49406_OK) return status;

    /* WR=1, EN=1 -> 0x0005 */
    status = AMT49406_WriteReg(AMT49406_REG_EEPROM_CTRL, 0x0005);
    if (status != AMT49406_OK) return status;

    Delay_Ms(16); /* High voltage write pulse */

    /* 3. Clear control register */
    AMT49406_WriteReg(AMT49406_REG_EEPROM_CTRL, 0x0000);

    return AMT49406_OK;
}

uint8_t AMT49406_EraseEEPROM(uint8_t eeAddr) {
    uint8_t status;

    status = AMT49406_WriteReg(AMT49406_REG_EEPROM_ADDR, eeAddr & 0x1F);
    if (status != AMT49406_OK) return status;

    status = AMT49406_WriteReg(AMT49406_REG_EEPROM_DATA_IN, 0x0000);
    if (status != AMT49406_OK) return status;

    /* ER=1, EN=1 -> 0x0003 */
    status = AMT49406_WriteReg(AMT49406_REG_EEPROM_CTRL, 0x0003);
    if (status != AMT49406_OK) return status;

    Delay_Ms(16); /* High voltage erase pulse */

    AMT49406_WriteReg(AMT49406_REG_EEPROM_CTRL, 0x0000);

    return AMT49406_OK;
}
