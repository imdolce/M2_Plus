#include "ws2812.h"
#include <debug.h>

static ws2812_color_t led_buffer[WS2812_NUM_LEDS];
static uint8_t global_brightness = WS2812_DEFAULT_BRIGHTNESS;

static const ws2812_color_t BATT_COLORS[WS2812_NUM_LEDS] = {
    COLOR_RED,     /* LED 0 (Lowest) */
    COLOR_ORANGE,  /* LED 1 */
    COLOR_LIME,    /* LED 2 */
    COLOR_GREEN    /* LED 3 (Highest) */
};

void ws2812_init(void) {
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);

    /* PD2: Output Push-Pull 30MHz */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    /* Set data line firmly LOW initially */
    GPIOD->BCR = GPIO_Pin_2;
    Delay_Us(100);

    ws2812_clear();
    ws2812_update();
}

void ws2812_set_brightness(uint8_t brightness) {
    global_brightness = brightness;
}

uint8_t ws2812_get_brightness(void) {
    return global_brightness;
}

static inline uint8_t scale_color(uint8_t val, uint8_t brightness) {
    return (uint8_t)(((uint16_t)val * brightness) / 255);
}

void ws2812_set_pixel(uint8_t index, ws2812_color_t color) {
    if (index < WS2812_NUM_LEDS) {
        led_buffer[index] = color;
    }
}

void ws2812_set_all(ws2812_color_t color) {
    for (uint8_t i = 0; i < WS2812_NUM_LEDS; i++) {
        led_buffer[i] = color;
    }
}

void ws2812_clear(void) {
    ws2812_set_all(COLOR_BLACK);
}

/*
 * Bit-bang one byte to WS2812B over PD2 at 48MHz system clock.
 * Timing:
 * '1': High ~750ns, Low ~500ns
 * '0': High ~290ns, Low ~915ns
 */
static void ws2812_send_byte(uint8_t byte) {
    for (int8_t i = 7; i >= 0; i--) {
        if (byte & (1 << i)) {
            /* Bit 1: High for ~36 cycles (750ns), Low for ~24 cycles (500ns) */
            GPIOD->BSHR = GPIO_Pin_2;
            __asm__ volatile(
                "nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n"
                "nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n"
                "nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n"
                "nop\n nop\n nop\n nop\n nop\n nop\n"
            );
            GPIOD->BCR = GPIO_Pin_2;
            __asm__ volatile(
                "nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n"
                "nop\n nop\n nop\n nop\n"
            );
        } else {
            /* Bit 0: High for ~14 cycles (290ns), Low for ~44 cycles (915ns) */
            GPIOD->BSHR = GPIO_Pin_2;
            __asm__ volatile(
                "nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n"
                "nop\n"
            );
            GPIOD->BCR = GPIO_Pin_2;
            __asm__ volatile(
                "nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n"
                "nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n"
                "nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n"
                "nop\n nop\n nop\n nop\n nop\n nop\n nop\n"
            );
        }
    }
}

void ws2812_update(void) {
    /* Critical timing section: disable interrupts during transmission (~120us) */
    __disable_irq();

    for (uint8_t i = 0; i < WS2812_NUM_LEDS; i++) {
        /* WS2812B expects GRB order */
        uint8_t g = scale_color(led_buffer[i].g, global_brightness);
        uint8_t r = scale_color(led_buffer[i].r, global_brightness);
        uint8_t b = scale_color(led_buffer[i].b, global_brightness);

        ws2812_send_byte(g);
        ws2812_send_byte(r);
        ws2812_send_byte(b);
    }

    __enable_irq();

    /* Hold data line LOW for reset latch (> 80us) */
    GPIOD->BCR = GPIO_Pin_2;
    Delay_Us(90);
}

/*
 * Show speed level (1 to 10) feedback:
 *   1-4:  Cyan   (1 to 4 LEDs respectively)
 *   5-8:  Purple (1 to 4 LEDs respectively)
 *   9-10: Lime   (2 LEDs for L9, 4 LEDs for L10)
 */
void ws2812_show_speed_level(uint8_t level) {
    ws2812_clear();
    if (level >= 1 && level <= 4) {
        for (uint8_t i = 0; i < level; i++) {
            ws2812_set_pixel(i, COLOR_CYAN);
        }
    } else if (level >= 5 && level <= 8) {
        uint8_t count = level - 4;
        for (uint8_t i = 0; i < count; i++) {
            ws2812_set_pixel(i, COLOR_PURPLE);
        }
    } else if (level >= 9 && level <= 10) {
        uint8_t count = (level == 9) ? 2 : 4;
        for (uint8_t i = 0; i < count; i++) {
            ws2812_set_pixel(i, COLOR_LIME);
        }
    }
    ws2812_update();
}

/*
 * Show battery level on 4 LEDs:
 * Order from lowest to highest:
 *   LED 0: Red
 *   LED 1: Orange
 *   LED 2: Lime
 *   LED 3: Green
 * bars: 0 to 4
 * is_low_battery: 1 if low battery (blinks LED 0 in Red)
 */
void ws2812_show_battery_bars(uint8_t bars, uint8_t is_low_battery, uint8_t blink_phase) {
    if (is_low_battery) {
        ws2812_clear();
        if (blink_phase) {
            ws2812_set_pixel(0, COLOR_RED);
        }
    } else {
        for (uint8_t i = 0; i < WS2812_NUM_LEDS; i++) {
            if (i < bars) {
                ws2812_set_pixel(i, BATT_COLORS[i]);
            } else {
                ws2812_set_pixel(i, COLOR_BLACK);
            }
        }
    }
    ws2812_update();
}

static inline ws2812_color_t color_scale(ws2812_color_t c, uint8_t factor) {
    ws2812_color_t out;
    out.r = (uint8_t)(((uint16_t)c.r * factor) / 255);
    out.g = (uint8_t)(((uint16_t)c.g * factor) / 255);
    out.b = (uint8_t)(((uint16_t)c.b * factor) / 255);
    return out;
}

static inline uint8_t calculate_breathing_factor_025hz(uint16_t phase_ms) {
    /* 0.25 Hz frequency -> Period T = 4000 ms */
    uint16_t t = phase_ms % 4000;
    uint32_t x;
    if (t < 2000) {
        x = ((uint32_t)t * 255) / 2000;
    } else {
        x = ((uint32_t)(4000 - t) * 255) / 2000;
    }

    /* Smooth S-curve (cubic ease in-out approximation) */
    uint32_t y;
    if (x < 128) {
        y = (2 * x * x) / 255;
    } else {
        uint32_t inv = 255 - x;
        y = 255 - ((2 * inv * inv) / 255);
    }

    /* Subtle breathing floor (15) to peak (255) */
    return (uint8_t)(15 + ((y * (255 - 15)) / 255));
}

/*
 * When charging:
 * Shows battery level with the highest level power indicator LED "breathing" at 0.25 Hz.
 * If fully charged (is_full = 1): all 4 LEDs solid ON (Red, Orange, Lime, Green).
 */
void ws2812_show_charging(uint8_t bars, uint16_t phase_ms, uint8_t is_full) {
    if (is_full) {
        for (uint8_t i = 0; i < WS2812_NUM_LEDS; i++) {
            ws2812_set_pixel(i, BATT_COLORS[i]);
        }
        ws2812_update();
        return;
    }

    /* Active highest level indicator index (0 to 3) */
    uint8_t active_idx = (bars > 0) ? (bars - 1) : 0;
    if (active_idx >= WS2812_NUM_LEDS) {
        active_idx = WS2812_NUM_LEDS - 1;
    }

    uint8_t breath_factor = calculate_breathing_factor_025hz(phase_ms);

    for (uint8_t i = 0; i < WS2812_NUM_LEDS; i++) {
        if (i < active_idx) {
            /* Lower active bars: Solid ON */
            ws2812_set_pixel(i, BATT_COLORS[i]);
        } else if (i == active_idx) {
            /* Highest active power indicator LED: Breathing at 0.25 Hz */
            ws2812_set_pixel(i, color_scale(BATT_COLORS[i], breath_factor));
        } else {
            /* Inactive upper bars: OFF */
            ws2812_set_pixel(i, COLOR_BLACK);
        }
    }
    ws2812_update();
}
