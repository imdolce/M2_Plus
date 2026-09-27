#ifndef __WS2812_H
#define __WS2812_H

#include <ch32v00X.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WS2812_NUM_LEDS              4
#define WS2812_DEFAULT_BRIGHTNESS    50   /* Brightness limit (0 - 255), defaults to 50 */

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} ws2812_color_t;

/* Standard Colors */
#define COLOR_BLACK     ((ws2812_color_t){0,   0,   0})
#define COLOR_RED       ((ws2812_color_t){255, 0,   0})
#define COLOR_ORANGE    ((ws2812_color_t){255, 80,  0})
#define COLOR_LIME      ((ws2812_color_t){50,  255, 0})
#define COLOR_GREEN     ((ws2812_color_t){0,   255, 0})
#define COLOR_CYAN      ((ws2812_color_t){0,   220, 255})
#define COLOR_PURPLE    ((ws2812_color_t){180, 0,   255})
#define COLOR_YELLOW    ((ws2812_color_t){255, 190, 0})

void ws2812_init(void);
void ws2812_set_brightness(uint8_t brightness);
uint8_t ws2812_get_brightness(void);

void ws2812_set_pixel(uint8_t index, ws2812_color_t color);
void ws2812_set_all(ws2812_color_t color);
void ws2812_clear(void);
void ws2812_update(void);

/* Application Display Modes */
void ws2812_show_speed_level(uint8_t level);
void ws2812_show_battery_bars(uint8_t bars, uint8_t is_low_battery, uint8_t blink_phase);
void ws2812_show_charging(uint8_t bars, uint16_t phase_ms, uint8_t is_full);

#ifdef __cplusplus
}
#endif

#endif /* __WS2812_H */
