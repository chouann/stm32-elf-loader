#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "kernel_api.h"
#include "main.h"

typedef struct {
    const char *name;
    void *addr;
} KernelSymbol_t;

static const KernelSymbol_t symbol_table[] = {
    {"kernel_printf", (void *) kernel_printf},
    {"kernel_gpio_write", (void *) kernel_gpio_write},
    {"kernel_gpio_read", (void *) kernel_gpio_read},
    {"kernel_delay_ms", (void *) kernel_delay_ms},
    {NULL, NULL}};

void kernel_api_init(void) {}

uint32_t kernel_symbol_lookup(const char *name)
{
    for (int i = 0; symbol_table[i].name != NULL; i++) {
        if (strcmp(symbol_table[i].name, name) == 0)
            return (uint32_t) symbol_table[i].addr;
    }
    return 0;
}

void kernel_printf(const char *fmt, ...)
{
    (void) fmt;
}

void kernel_gpio_write(uint16_t pin, uint8_t val)
{
    HAL_GPIO_WritePin(GPIOD, pin, val ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

uint8_t kernel_gpio_read(uint16_t pin)
{
    return (uint8_t) HAL_GPIO_ReadPin(GPIOD, pin);
}

void kernel_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}
