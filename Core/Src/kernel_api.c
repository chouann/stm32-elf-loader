#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "stream_buffer.h"
#include "task.h"

#include "kernel_api.h"
#include "stm32f4xx_hal.h"
#include "wifi.h"

extern UART_HandleTypeDef huart2;

#define PRINTF_BUF_SIZE 128
/* Larger than SHELL_MAX_LINE so pasted commands are not dropped */
#define RX_STREAM_SIZE 128
#define UART_TX_TIMEOUT_MS 100
/* Finite mutex wait: a dynamic task killed mid-print dies holding
 * uart_mutex, and a portMAX_DELAY take would hang the shell forever.
 * Dropping output (counted in tx_errors) keeps the shell alive. */
#define UART_MUTEX_TIMEOUT_MS 100

static SemaphoreHandle_t uart_mutex;
static StreamBufferHandle_t rx_stream;
static uint8_t rx_byte;
/* Diagnostics counters, readable from a debugger */
static volatile uint32_t rx_dropped;
static volatile uint32_t tx_errors;

static int uart_start_rx(void)
{
    return HAL_UART_Receive_IT(&huart2, &rx_byte, 1) == HAL_OK ? 0 : -1;
}

void kernel_uart_rx_isr(void)
{
    BaseType_t woken = pdFALSE;
    if (xStreamBufferSendFromISR(rx_stream, &rx_byte, 1, &woken) != 1)
        rx_dropped++;
    if (uart_start_rx() != 0)
        rx_dropped++;
    portYIELD_FROM_ISR(woken);
}
void kernel_uart_resume_rx(void)
{
    uart_start_rx();
}

/* Create RTOS objects only; called before the scheduler starts. */
int kernel_init(void)
{
    uart_mutex = xSemaphoreCreateMutex();
    rx_stream = xStreamBufferCreate(RX_STREAM_SIZE, 1);
    if (!uart_mutex || !rx_stream) {
        if (uart_mutex)
            vSemaphoreDelete(uart_mutex);
        if (rx_stream)
            vStreamBufferDelete(rx_stream);
        uart_mutex = NULL;
        rx_stream = NULL;
        return -1;
    }
    return wifi_dma_init();
}

/* Start UART reception; ISRs call FreeRTOS FromISR APIs, so this must run
 * after the scheduler has started.  Returns -1 if shell RX could not be
 * started (the shell would otherwise hang silently in kernel_getchar). */
int kernel_io_start(void)
{
    HAL_NVIC_SetPriority(USART2_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    int rc = uart_start_rx();
    wifi_start_rx();
    return rc;
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
    if (!uart_mutex)
        return;

    char buf[PRINTF_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len > 0) {
        if (len > (int) sizeof(buf) - 1)
            len = sizeof(buf) - 1;
        if (xSemaphoreTake(uart_mutex, pdMS_TO_TICKS(UART_MUTEX_TIMEOUT_MS)) !=
            pdTRUE) {
            tx_errors++;
            return;
        }
        if (HAL_UART_Transmit(&huart2, (uint8_t *) buf, len,
                              UART_TX_TIMEOUT_MS) != HAL_OK)
            tx_errors++;
        xSemaphoreGive(uart_mutex);
    }
}

void kernel_putchar(char ch)
{
    if (!uart_mutex)
        return;
    if (xSemaphoreTake(uart_mutex, pdMS_TO_TICKS(UART_MUTEX_TIMEOUT_MS)) !=
        pdTRUE) {
        tx_errors++;
        return;
    }
    if (HAL_UART_Transmit(&huart2, (uint8_t *) &ch, 1, UART_TX_TIMEOUT_MS) !=
        HAL_OK)
        tx_errors++;
    xSemaphoreGive(uart_mutex);
}

int kernel_getchar(void)
{
    if (!rx_stream)
        return -1;
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
