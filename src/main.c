#include <ch32v00X.h>
#include <debug.h>
#include "amt49406.h"
#include "config_manager.h"
#include "ws2812.h"
#include "battery.h"
#include "button.h"

/* Operating States */
typedef enum {
    STATE_OFF = 0,
    STATE_RUNNING
} app_state_t;

/* 10 Speed Throttle Mapping Table (Demand 0 to 511)
 * Discrete levels from 11% (57) to 100% (511).
 * (Demand <= 10% / 51 is interpreted as 0% stop by AMT49406). */
static const uint16_t SPEED_THROTTLE_TABLE[11] = {
    0,    /* Level 0: Off */
    57,   /* Level 1: 11% */
    107,  /* Level 2: 21% */
    158,  /* Level 3: 31% */
    210,  /* Level 4: 41% */
    261,  /* Level 5: 51% */
    312,  /* Level 6: 61% */
    363,  /* Level 7: 71% */
    414,  /* Level 8: 81% */
    465,  /* Level 9: 91% */
    511   /* Level 10: 100% */
};

#define SPEED_FEEDBACK_DELAY_MS     1500  /* 1.5s delay before reverting to battery status */

/* Interrupt Handlers */
void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI7_0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void NMI_Handler(void) {}
void HardFault_Handler(void) {
    /* If a hard fault occurs, set LEDs to solid red and halt */
    ws2812_set_all(COLOR_RED);
    ws2812_update();
    while (1);
}

void EXTI7_0_IRQHandler(void) {
    if (EXTI_GetITStatus(EXTI_Line5) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line5);
    }
    if (EXTI_GetITStatus(EXTI_Line1) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line1);
    }
}

int main(void) {
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();

    /* 1. Initialize Peripherals */
    ws2812_init();
    button_init();
    Battery_Init();
    AMT49406_GPIO_Init();
    AMT49406_I2C_Init();

    /* 2. AMT49406 Power Window Capture & I2C Handshake */
    uint16_t elapsed = 0;
    AMT49406_Capture_Boot_Window(800, &elapsed);

    /* 3. EEPROM Auto-Verification & Self-Healing */
    config_status_t cfg_status = Config_VerifyEEPROM();

    if (cfg_status == CONFIG_STATUS_MISMATCH) {
        /* Config mismatch or fresh unprogrammed chip: Auto-flash golden configuration */
        cfg_status = Config_FlashEEPROM();

        if (cfg_status == CONFIG_STATUS_MATCH) {
            /* Flashing successful: Set LEDs to Yellow.
             * User/programmer must power cycle the system once to apply non-volatile registers. */
            ws2812_set_all(COLOR_YELLOW);
            ws2812_update();
            while (1) {
                Delay_Ms(500);
            }
        } else {
            /* Flash verification failed: Set LEDs to Red and halt */
            ws2812_set_all(COLOR_RED);
            ws2812_update();
            while (1) {
                Delay_Ms(500);
            }
        }
    } else if (cfg_status == CONFIG_STATUS_ERROR) {
        /* I2C communication error: Try bus recovery and retry once */
        AMT49406_I2C_Bus_Recover();
        cfg_status = Config_VerifyEEPROM();
        if (cfg_status != CONFIG_STATUS_MATCH) {
            ws2812_set_all(COLOR_RED);
            ws2812_update();
            while (1) {
                Delay_Ms(500);
            }
        }
    }

    /* Verification Passed: Set all LEDs to Green for 1.2s to confirm check OK */
    ws2812_set_all(COLOR_GREEN);
    ws2812_update();
    Delay_Ms(1200);

    /* 4. Start in OFF state waiting for user button press or charger */
    app_state_t state = STATE_OFF;
    uint8_t current_level = 0;
    uint16_t charging_phase_ms = 0;
    uint8_t blink_counter = 0;
    uint16_t speed_feedback_timer_ms = 0;
    uint16_t off_adc_timer_ms = 0;
    uint16_t running_adc_timer_ms = 0;
    uint8_t prev_charging = 0;

    AMT49406_SetSpeed(0);
    AMT49406_SetBrake(0);
    Battery_ResetChargeDetect();
    ws2812_clear();
    ws2812_update();

    while (1) {
        /* --------------------------------------------------------
         * STATE: OFF (Fan speed 0)
         * -------------------------------------------------------- */
        if (state == STATE_OFF) {
            /* 1. Poll button every 20ms */
            button_poll(20);
            if (button_get_event() == BUTTON_EVENT_SHORT_PRESS) {
                state = STATE_RUNNING;
                current_level = 1; /* Start at Level 1 (11%) */
                speed_feedback_timer_ms = SPEED_FEEDBACK_DELAY_MS;
                AMT49406_SetSpeed(SPEED_THROTTLE_TABLE[current_level]);
                running_adc_timer_ms = 0;
                continue;
            }

            /* 2. Poll ADC every 500ms */
            off_adc_timer_ms += 20;
            if (off_adc_timer_ms >= 500) {
                off_adc_timer_ms = 0;
                Battery_Update(0);
            }

            /* 3. LED Behavior & Power Saving in OFF state:
             * If charge detection pin > 1.5V: Turn on battery LEDs & indicate charge status
             * If not charging: All LEDs stay completely OFF */
            if (Battery_IsCharging()) {
                charging_phase_ms = (charging_phase_ms + 20) % 4000; /* 0.25 Hz breathing (4000ms period) */
                ws2812_show_charging(Battery_GetBars(), charging_phase_ms, Battery_IsFullCharge());
                prev_charging = 1;
            } else {
                if (prev_charging) {
                    ws2812_clear();
                    ws2812_update();
                    prev_charging = 0;
                }
                /* Configure EXTI on PD5 (button falling) and PA1 (charger rising) for sleep wakeup */
                button_prepare_sleep_exti();
                Battery_Prepare_Sleep_EXTI();
                __WFI();
            }

            Delay_Ms(20);
            continue;
        }

        /* --------------------------------------------------------
         * STATE: RUNNING (Fan speed Level 1 to 10)
         * -------------------------------------------------------- */
        if (state == STATE_RUNNING) {
            button_poll(20);
            button_event_t btn_ev = button_get_event();

            /* Short Press: Cycle to next wind level (1 to 10), or turn off if beyond max (10) */
            if (btn_ev == BUTTON_EVENT_SHORT_PRESS) {
                current_level++;
                if (current_level > 10) {
                    /* Click at max throttle (level 10) -> Turn OFF fan and all LEDs */
                    AMT49406_SetSpeed(0);
                    AMT49406_SetBrake(1);
                    Delay_Ms(200);
                    AMT49406_SetBrake(0);

                    current_level = 0;
                    state = STATE_OFF;
                    off_adc_timer_ms = 0;
                    prev_charging = 0;
                    Battery_ResetChargeDetect();
                    ws2812_clear();
                    ws2812_update();
                    continue;
                }
                AMT49406_SetSpeed(SPEED_THROTTLE_TABLE[current_level]);
                speed_feedback_timer_ms = SPEED_FEEDBACK_DELAY_MS;
            }
            /* Long Press (1.5s): Turn off immediately */
            else if (btn_ev == BUTTON_EVENT_LONG_PRESS) {
                /* Press and hold for 1.5s -> Turn OFF fan and all LEDs */
                AMT49406_SetSpeed(0);
                AMT49406_SetBrake(1);
                Delay_Ms(200);
                AMT49406_SetBrake(0);

                current_level = 0;
                state = STATE_OFF;
                off_adc_timer_ms = 0;
                prev_charging = 0;
                Battery_ResetChargeDetect();
                ws2812_clear();
                ws2812_update();
                continue;
            }

            /* Poll ADC every 500ms while running */
            running_adc_timer_ms += 20;
            if (running_adc_timer_ms >= 500) {
                running_adc_timer_ms = 0;
                Battery_Update(current_level);
            }

            /* Check critical battery cutoff (< 3.05V) */
            if (Battery_IsCutoff()) {
                AMT49406_SetSpeed(0);
                AMT49406_SetBrake(1);
                for (uint8_t i = 0; i < 3; i++) {
                    ws2812_set_all(COLOR_RED);
                    ws2812_update();
                    Delay_Ms(250);
                    ws2812_clear();
                    ws2812_update();
                    Delay_Ms(250);
                }
                AMT49406_SetBrake(0);
                current_level = 0;
                state = STATE_OFF;
                off_adc_timer_ms = 0;
                prev_charging = 0;
                Battery_ResetChargeDetect();
                ws2812_clear();
                ws2812_update();
                continue;
            }

            /* LED Display Logic while running:
             * 1. If speed level just changed: Display speed feedback (1-4 Cyan, 5-8 Purple, 9-10 Lime) for 1.5s
             * 2. After 1.5s timeout:
             *    - If charging: Indicate charge status (0.25 Hz breathing)
             *    - If not charging: Display normal battery bars (Red, Orange, Lime, Green) */
            if (speed_feedback_timer_ms > 0) {
                speed_feedback_timer_ms = (speed_feedback_timer_ms >= 20) ? (speed_feedback_timer_ms - 20) : 0;
                ws2812_show_speed_level(current_level);
            } else {
                if (Battery_IsCharging()) {
                    charging_phase_ms = (charging_phase_ms + 20) % 4000; /* 0.25 Hz breathing (4000ms period) */
                    ws2812_show_charging(Battery_GetBars(), charging_phase_ms, Battery_IsFullCharge());
                } else {
                    blink_counter = (blink_counter + 1) % 25;
                    ws2812_show_battery_bars(Battery_GetBars(), Battery_IsLow(), (blink_counter < 12));
                }
            }

            Delay_Ms(20);
        }
    }
}