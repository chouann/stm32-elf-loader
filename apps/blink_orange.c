#include "kernel.h"

#define LED_ORANGE 0x2000

void app_main(void *args)
{
    (void) args;
    while (1) {
        kernel_gpio_write(LED_ORANGE, 1);
        kernel_delay_ms(200);
        kernel_gpio_write(LED_ORANGE, 0);
        kernel_delay_ms(200);
    }
}

void app_cleanup(void)
{
    kernel_gpio_write(LED_ORANGE, 0);
}
