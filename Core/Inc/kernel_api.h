#pragma once

#include <stdint.h>
#include "wifi.h" // let Core/Src/stm32f4xx_it.c can access wifi_handle_uart_rx_event(size)
void kernel_init(void);
void kernel_uart_rx_isr(void);
void kernel_uart_resume_rx(void);
uint32_t kernel_lookup(const char *name);

void kernel_printf(const char *fmt, ...);
void kernel_putchar(char ch);
int kernel_getchar(void);
void kernel_gpio_write(uint16_t pin, uint8_t val);
uint8_t kernel_gpio_read(uint16_t pin);
void kernel_delay_ms(uint32_t ms);
