#include "kernel.h"

#define LED_GREEN 0x1000

void app_main(void *args)
{
    (void) args;
    while (1) {
        kernel_gpio_write(LED_GREEN, 1);
        kernel_delay_ms(500);
        kernel_gpio_write(LED_GREEN, 0);
        kernel_delay_ms(500);
    }
}
