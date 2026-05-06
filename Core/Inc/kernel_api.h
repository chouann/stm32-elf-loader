#pragma once

#include <stdint.h>

void kernel_api_init(void);
uint32_t kernel_symbol_lookup(const char *name);

void kernel_printf(const char *fmt, ...);
void kernel_gpio_write(uint16_t pin, uint8_t val);
uint8_t kernel_gpio_read(uint16_t pin);
void kernel_delay_ms(uint32_t ms);
