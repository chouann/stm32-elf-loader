#pragma once
#include "fatfs.h"
#include "myprintf.h"
#include "stream_buffer.h"
#include "semphr.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define RX_BUFFER_SIZE 512
#define WIFI_RX_STREAM_SIZE RX_BUFFER_SIZE * 4
#define PC_SERVER_IP "172.23.38.43"     // When STM32 is sender, update with actual PC STAIP
#define TCP_SERVER_PORT 8080             // TCP Server port

#define WIFINAME "Ann" // for AT+CWJAP
#define WIFIPASSWORD "Ann12345" // for AT+CWJAP
// Response detection flags - used after receive messages
typedef struct {
    uint8_t has_ok;          // "OK" marker found
    uint8_t has_fail;        // "FAIL" marker found
    uint8_t has_error;       // "ERROR" marker found
    uint8_t has_busy;        // "busy" marker found (transient state)
    uint8_t has_cwjap;       // "+CWJAP:" WiFi response found
    uint8_t has_prompt;      // ">" prompt found (ready for data)
    uint8_t has_ipd;         // "+IPD," incoming data marker found
    uint8_t has_connect;     // "CONNECT" unsolicited message (connection established)
    uint8_t has_closed;      // "CLOSED" unsolicited message (connection closed)
    uint8_t has_fname;      // "FNAME" used when receiving file, First Message format is "FNAME=green.o,FSIZE=450"
    uint8_t has_fsize;      // "FSIZE" used when receiving file, First Message format is "FNAME=green.o,FSIZE=450"
} WIFI_RESPONSE_FLAGS;

// Function prototypes
int wifi_send_cmd(const char *cmd, uint32_t timeout_ms);
int wifi_init(void);
int wifi_receive_file();
int wifi_receive_msg();

// Callback initialization function (call from HAL_UARTEx_RxEventCallback in main.c)
void wifi_handle_uart_rx_event(uint16_t size);