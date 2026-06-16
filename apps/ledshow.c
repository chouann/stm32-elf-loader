#include "kernel.h"

#define LED_GREEN 0x1000  /* PD12 */
#define LED_ORANGE 0x2000 /* PD13 */
#define LED_RED 0x4000    /* PD14 */
#define LED_BLUE 0x8000   /* PD15 */

static const uint16_t leds[] = {LED_GREEN, LED_ORANGE, LED_RED, LED_BLUE};
#define NUM_LEDS (sizeof(leds) / sizeof(leds[0]))

void app_main(void *args)
{
    (void) args;
    uint32_t i = 0;

    while (1) {
        /* Turn off all, then light the current one */
        for (uint32_t j = 0; j < NUM_LEDS; j++)
            kernel_gpio_write(leds[j], 0);
        kernel_gpio_write(leds[i % NUM_LEDS], 1);
        i++;
        kernel_delay_ms(50);
    }
}

void app_cleanup(void)
{
    for (uint32_t j = 0; j < NUM_LEDS; j++)
        kernel_gpio_write(leds[j], 0);
}
