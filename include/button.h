#ifndef __BUTTON_H
#define __BUTTON_H

#include <ch32v00X.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUTTON_EVENT_NONE = 0,
    BUTTON_EVENT_SHORT_PRESS,
    BUTTON_EVENT_LONG_PRESS
} button_event_t;

void button_init(void);
uint8_t button_is_down(void);
void button_poll(uint16_t delta_ms);
button_event_t button_get_event(void);

/* Configure PD5 EXTI falling edge for low power sleep wakeup */
void button_prepare_sleep_exti(void);

#ifdef __cplusplus
}
#endif

#endif /* __BUTTON_H */
