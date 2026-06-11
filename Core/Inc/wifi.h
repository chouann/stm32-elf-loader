#pragma once

#include <stdint.h>

#define WIFI_SSID "F002"
#define WIFI_PASSWORD "ch-f02ef"
#define WIFI_SERVER_PORT 8080

int wifi_init(void);
int wifi_receive_file(void);
int wifi_receive_msg(void);

int wifi_dma_init(void);
void wifi_start_rx(void);
void wifi_handle_uart_rx_event(uint16_t size);
void wifi_uart_resume_rx(void);
