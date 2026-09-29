#include "button.h"
#include <debug.h>

#define DEBOUNCE_TIME_MS      20
#define LONG_PRESS_TIME_MS    1500

static uint16_t press_duration_ms = 0;
static uint8_t  last_pin_state = 0;
static uint8_t  long_press_triggered = 0;
static button_event_t pending_event = BUTTON_EVENT_NONE;

void button_init(void) {
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);

    /* PD5: Input Pull-Up (hardware 10k pull-up to 5V also present) */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    press_duration_ms = 0;
    last_pin_state = button_is_down();
    long_press_triggered = 0;
    pending_event = BUTTON_EVENT_NONE;
}

uint8_t button_is_down(void) {
    /* Active LOW:
     * Released: pin is HIGH (Bit_SET) -> returns 0
     * Pressed:  pin connects to GND (Bit_RESET) -> returns 1 */
    return (GPIO_ReadInputDataBit(GPIOD, GPIO_Pin_5) == Bit_RESET) ? 1 : 0;
}

void button_poll(uint16_t delta_ms) {
    uint8_t pin_is_down = button_is_down();

    if (pin_is_down) {
        press_duration_ms += delta_ms;

        /* Check for Long Press (>= 1.5s) */
        if (!long_press_triggered && press_duration_ms >= LONG_PRESS_TIME_MS) {
            long_press_triggered = 1;
            pending_event = BUTTON_EVENT_LONG_PRESS;
        }
    } else {
        /* Pin released: generate short press if debounced and wasn't a long press */
        if (last_pin_state && !long_press_triggered && press_duration_ms >= DEBOUNCE_TIME_MS) {
            pending_event = BUTTON_EVENT_SHORT_PRESS;
        }
        press_duration_ms = 0;
        long_press_triggered = 0;
    }

    last_pin_state = pin_is_down;
}

button_event_t button_get_event(void) {
    button_event_t ev = pending_event;
    pending_event = BUTTON_EVENT_NONE;
    return ev;
}
