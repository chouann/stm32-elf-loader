#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "stream_buffer.h"
#include "task.h"

#include "kernel_api.h"
#include "stm32f4xx_hal.h"

extern UART_HandleTypeDef huart2;

#define PRINTF_BUF_SIZE 128
#define RX_STREAM_SIZE 32

static SemaphoreHandle_t uart_mutex;
static StreamBufferHandle_t rx_stream;
static uint8_t rx_byte;

static void uart_start_rx(void)
{
    HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
}

void kernel_uart_rx_isr(void)
{
    BaseType_t woken = pdFALSE;
    xStreamBufferSendFromISR(rx_stream, &rx_byte, 1, &woken);
    uart_start_rx();
    portYIELD_FROM_ISR(woken);
}

void kernel_init(void)
{
    uart_mutex = xSemaphoreCreateMutex();
    rx_stream = xStreamBufferCreate(RX_STREAM_SIZE, 1);
    HAL_NVIC_SetPriority(USART2_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    uart_start_rx();
}

typedef struct {
    const char *name;
    void *addr;
} kernel_symbol_t;

static const kernel_symbol_t symbol_table[] = {
    {"kernel_printf", (void *) kernel_printf},
    {"kernel_gpio_write", (void *) kernel_gpio_write},
    {"kernel_gpio_read", (void *) kernel_gpio_read},
    {"kernel_delay_ms", (void *) kernel_delay_ms},
    {NULL, NULL}};

uint32_t kernel_lookup(const char *name)
{
    for (int i = 0; symbol_table[i].name != NULL; i++) {
        if (strcmp(symbol_table[i].name, name) == 0)
            return (uint32_t) symbol_table[i].addr;
    }
    return 0;
}

void kernel_printf(const char *fmt, ...)
{
    char buf[PRINTF_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len > 0) {
        if (len > (int) sizeof(buf) - 1)
            len = sizeof(buf) - 1;
        xSemaphoreTake(uart_mutex, portMAX_DELAY);
        HAL_UART_Transmit(&huart2, (uint8_t *) buf, len, HAL_MAX_DELAY);
        xSemaphoreGive(uart_mutex);
    }
}

void kernel_putchar(char ch)
{
    xSemaphoreTake(uart_mutex, portMAX_DELAY);
    HAL_UART_Transmit(&huart2, (uint8_t *) &ch, 1, HAL_MAX_DELAY);
    xSemaphoreGive(uart_mutex);
}

int kernel_getchar(void)
{
    uint8_t ch;
    if (xStreamBufferReceive(rx_stream, &ch, 1, portMAX_DELAY) == 1)
        return ch;
    return -1;
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
