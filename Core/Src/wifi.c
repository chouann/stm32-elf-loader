#include "FreeRTOS.h"
#include "fatfs.h"
#include "stream_buffer.h"
#include "semphr.h"
#include "wifi.h"
#include "kernel_api.h" // in order to use kernel_printf()
#define RX_BUFFER_SIZE 512
#define WIFI_RX_STREAM_SIZE RX_BUFFER_SIZE * 4
static SemaphoreHandle_t wifi_uart_mutex;
static StreamBufferHandle_t wifi_rx_stream;
static uint8_t dma_tmp_buffer[RX_BUFFER_SIZE];  // Receive buffer

extern UART_HandleTypeDef huart3;

static void wifi_uart_start_rx(void)
{   // non-blocking, DMA with interrupt type
    HAL_UARTEx_ReceiveToIdle_DMA(&huart3, dma_tmp_buffer, sizeof(dma_tmp_buffer));
}
void wifi_uart_resume_rx(void)
{   
    wifi_uart_start_rx();
}
/* Called from HAL_UARTEx_RxEventCallback */
void wifi_handle_uart_rx_event(uint16_t size)
{
    if (wifi_rx_stream == NULL) return;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    xStreamBufferSendFromISR(wifi_rx_stream, dma_tmp_buffer, size, &xHigherPriorityTaskWoken);
    
    wifi_uart_start_rx();

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
/* Create mutex and stream buffer and start receiving data by it-dma */
void wifi_dma_init(void) // used in kernel_api.c
{
    wifi_uart_mutex = xSemaphoreCreateMutex();
    wifi_rx_stream = xStreamBufferCreate(WIFI_RX_STREAM_SIZE, 1);
    wifi_uart_start_rx();
}
/* Internal Reset Function: clean stream buffer */
static void wifi_reset_rx_state(void)
{
    if (wifi_rx_stream != NULL) {
        xStreamBufferReset(wifi_rx_stream);
    }
}

/* Response Detection Helper Function */
static void wifi_detect_response_markers(const uint8_t *buffer, uint32_t len, WIFI_RESPONSE_FLAGS *flags)
{
    // Initialize all flags to 0
    memset(flags, 0, sizeof(WIFI_RESPONSE_FLAGS));
    
    if (buffer == NULL || len == 0) {
        return;
    }
    
    // Detect response markers in the buffer
    char *buf_check = (char *)buffer;
    flags->has_ok = strstr(buf_check, "OK") != NULL;
    flags->has_fail = strstr(buf_check, "FAIL") != NULL;
    flags->has_error = strstr(buf_check, "ERROR") != NULL;
    flags->has_busy = strstr(buf_check, "busy") != NULL;
    flags->has_cwjap = strstr(buf_check, "+CWJAP:") != NULL;
    flags->has_prompt = strchr(buf_check, '>') != NULL;
    flags->has_ipd = strstr(buf_check, "+IPD,") != NULL;
    flags->has_connect = strstr(buf_check, "CONNECT") != NULL;
    flags->has_closed = strstr(buf_check, "CLOSED") != NULL;
    flags->has_fname = strstr(buf_check, "FNAME") != NULL;
    flags->has_fsize = strstr(buf_check, "FSIZE") != NULL;
}
/* Send cmd "AT+..." and "receive" during period of timeout_ms ms */
static int wifi_send_cmd(const char *cmd, uint32_t timeout_ms)
{
    if (cmd == NULL || wifi_rx_stream == NULL) {
        return -1;
    }
    
    uint8_t temp_buffer[RX_BUFFER_SIZE * 2];
    uint32_t temp_len = 0;
    
    wifi_reset_rx_state();
    xSemaphoreTake(wifi_uart_mutex, portMAX_DELAY);
    HAL_UART_Transmit(&huart3, (uint8_t *)cmd, strlen(cmd), 100);
    xSemaphoreGive(wifi_uart_mutex);

    uint32_t start_time = HAL_GetTick();
    uint32_t timeout = start_time + timeout_ms;
    
    while (HAL_GetTick() < timeout) {
        size_t available_space = sizeof(temp_buffer) - temp_len - 1;
        if (available_space == 0) {
            temp_len = 0; 
            available_space = sizeof(temp_buffer) - 1;
            memset(temp_buffer, 0, sizeof(temp_buffer));
        }
        
        uint32_t remaining_ms = timeout - HAL_GetTick();
        if (remaining_ms > timeout_ms) break; // Underflow

        size_t received = xStreamBufferReceive(wifi_rx_stream, temp_buffer + temp_len, available_space, pdMS_TO_TICKS(remaining_ms));
        
        if (received > 0) {
            temp_len += received;
            temp_buffer[temp_len] = '\0';
            
            WIFI_RESPONSE_FLAGS resp_flags;
            wifi_detect_response_markers(temp_buffer, temp_len, &resp_flags);
            
            if (resp_flags.has_ok || resp_flags.has_fail || resp_flags.has_error || resp_flags.has_prompt || resp_flags.has_cwjap) {
                break;
            }
        }
    }

    if (temp_len > 0) {
        kernel_printf("%s", (char *)temp_buffer);
        WIFI_RESPONSE_FLAGS final_flags;
        wifi_detect_response_markers(temp_buffer, temp_len, &final_flags);

        if (final_flags.has_ok || final_flags.has_prompt) return 0;
        if (final_flags.has_cwjap && strstr((char *)temp_buffer, "+CWJAP:0") != NULL) return 0;
    } 
    return -1;
}
int wifi_init(void)
{
    /* This init is used for stm32 as server, outside clients send message to it. */
    int rsp;
    
    // 1. Check connection to wifi module
    if((rsp = wifi_send_cmd("AT\r\n", 1000)) != 0)
        return rsp;
    
    // 2. Set to station mode which can connect wifi
    if((rsp = wifi_send_cmd("AT+CWMODE=1\r\n", 1000)) != 0)
        return rsp;
    
    // 3. Connect to WiFi
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFINAME, WIFIPASSWORD);
    rsp = wifi_send_cmd(cmd, 1000000);
    if(rsp != 0) {
        return rsp;
    }
    
    // 4. Get IP address
    if((rsp = wifi_send_cmd("AT+CIFSR\r\n", 2000)) != 0)
        return rsp;

    // 5. Enable multi-connection mode (REQUIRED for TCP Server)
    if((rsp = wifi_send_cmd("AT+CIPMUX=1\r\n", 2000)) != 0) {
        return rsp;
    }
    
    // 6. Start TCP Server on port 8080
    snprintf(cmd, sizeof(cmd), "AT+CIPSERVER=1,%d\r\n", TCP_SERVER_PORT);
    if((rsp = wifi_send_cmd(cmd, 2000)) != 0) {
        return rsp;
    }
    
    return 0;
}
/* Receive a whole file from client 
   Return after writing whole file(written size must be equal to the FSIZE in first msg) into new file or socket is closed or timeout. 
*/
int wifi_receive_file(void)
{
    char filename[32] = "default.o"; // When length of filename is too long, it will use default name.
    uint32_t filesize = 0;
    FRESULT res;
    FIL file;
    uint8_t f_is_open = 0;
    uint32_t byteswrite;

    uint8_t temp_buffer[RX_BUFFER_SIZE * 2];
    uint32_t temp_len = 0;
    
    wifi_reset_rx_state();
    
    uint32_t timeout_ms = 60000; // 60 seconds
    uint32_t start_time = HAL_GetTick();
    uint32_t timeout = start_time + timeout_ms;
    
    while (HAL_GetTick() < timeout) {
        size_t available_space = sizeof(temp_buffer) - temp_len - 1;
        if (available_space == 0) {
            temp_len = 0; 
            available_space = sizeof(temp_buffer) - 1;
            memset(temp_buffer, 0, sizeof(temp_buffer));
        }

        uint32_t remaining_ms = timeout - HAL_GetTick();
        if (remaining_ms > timeout_ms) break; 
        
        size_t received = xStreamBufferReceive(wifi_rx_stream, temp_buffer + temp_len, available_space, pdMS_TO_TICKS(remaining_ms));
        
        if (received > 0) {
            temp_len += received;
            temp_buffer[temp_len] = '\0';
            
            WIFI_RESPONSE_FLAGS rx_flags;
            wifi_detect_response_markers(temp_buffer, temp_len, &rx_flags);
            if(!(rx_flags.has_ipd && !rx_flags.has_fname)) // don't print file data, just connect, closed, "+IPD,0,n,FNAME=AAA.o,FSIZE=450" text
                kernel_printf("%s", (char *)(temp_buffer + temp_len - received));

            // the format of first message containing filename would be "+IPD,0,n,FNAME=AAA.o,FSIZE=450"
            if (!f_is_open) {
                if (rx_flags.has_fname && rx_flags.has_fsize) {
                    char *fname_ptr = strstr((char *)temp_buffer, "FNAME=");
                    char *fsize_ptr = strstr((char *)temp_buffer, "FSIZE=");
                    if (fname_ptr && fsize_ptr) {
                        fname_ptr += 6;
                    }
                    else
                        return -1;
                    char *comma = strchr(fname_ptr, ',');
                    if (comma) {
                        int name_len = comma - fname_ptr;
                        if (name_len > 0 && name_len < sizeof(filename)) {
                            strncpy(filename, fname_ptr, name_len);
                            filename[name_len] = '\0';
                        }
                    }
                    else
                        return -1;
                    fsize_ptr += 6;
                    filesize = atoi(fsize_ptr);
                    kernel_printf("[wifi_receive_file] Try to open %s, size %d ...\r\n", filename, filesize);
                    if(filesize == 0)
                        return -1;
                    
                    res = f_open(&file, filename, FA_CREATE_ALWAYS | FA_WRITE);
                    if(res == FR_OK) {
                        f_is_open = 1;
                        kernel_printf("[wifi_receive_file] Opening %s succeeded.\r\n", filename);
                        // Start to shift out this +IPD,0,n,FNAME=aaa,FSIZE=125 part from temp_buffer
                        char *ipd_ptr = strstr((char *)temp_buffer, "+IPD,"); 
                        if (ipd_ptr) {
                            char *colon_ptr = strchr(ipd_ptr, ':');
                            if (colon_ptr) {
                                char *len_str = ipd_ptr + 5; 
                                char *comma_ptr = strchr(ipd_ptr + 5, ',');
                                if (comma_ptr && comma_ptr < colon_ptr) {
                                    len_str = comma_ptr + 1; 
                                }
                                int expected_data_len = atoi(len_str);
                                char *data_start = colon_ptr + 1;
                                uint32_t consumed_len = (data_start - (char *)temp_buffer) + expected_data_len;
                                
                                if (consumed_len <= temp_len) {
                                    uint32_t remaining = temp_len - consumed_len;
                                    memmove(temp_buffer, temp_buffer + consumed_len, remaining);
                                    temp_len = remaining;
                                    temp_buffer[temp_len] = '\0';
                                    wifi_detect_response_markers(temp_buffer, temp_len, &rx_flags);
                                } 
                                else {
                                    temp_len = 0;
                                }
                            } 
                            else {
                                temp_len = 0;
                            }
                        } 
                        else {
                            temp_len = 0;
                        }
                    } else {
                        kernel_printf("[wifi_receive_file] ERROR: Failed to open %s. f_open's return value=%d\r\n", filename, res);
                        return -1;
                    }
                    continue; 
                }
            }
            
            if (f_is_open && filesize > 0) {
                while (rx_flags.has_ipd) {
                    char *ipd_ptr = strstr((char *)temp_buffer, "+IPD,"); // the message of ipd format is "+IPD,0,5:aaaa\n"
                    if (!ipd_ptr) {
                        rx_flags.has_ipd = 0; // Clear flag if not found after buffer shift
                        break; 
                    }
                    
                    char *colon_ptr = strchr(ipd_ptr, ':');
                    if (!colon_ptr) break;
                    
                    char *len_str = ipd_ptr + 5; 
                    char *comma_ptr = strchr(ipd_ptr + 5, ',');
                    if (comma_ptr && comma_ptr < colon_ptr) {
                        len_str = comma_ptr + 1; 
                    }
                    int expected_data_len = atoi(len_str); // get 5 in "+IPD,0,5:aaaa\n", 5 is message length
                    
                    char *data_start = colon_ptr + 1; // get "aaaa" in "+IPD,0,5:aaaa\n"
                    uint32_t current_msg_len = temp_len - (data_start - (char *)temp_buffer);
                    
                    if (current_msg_len >= expected_data_len) {
                        res = f_write(&file, data_start, expected_data_len, (void *)&byteswrite);
                        
                        if (res != FR_OK || expected_data_len != byteswrite) {
                            kernel_printf("[wifi_receive_file] ERROR: f_write failed or incomplete. res=%d, wrote=%u, expected=%u\r\n", res, byteswrite, expected_data_len);
                            f_close(&file);
                            return -1;
                        }
                        
                        filesize -= byteswrite;
                        kernel_printf("[wifi_receive_file] f_write succeeded. Remaining written filesize=%u\r\n", filesize);
                        
                        uint32_t consumed_len = (data_start - (char *)temp_buffer) + expected_data_len;
                        uint32_t remaining = temp_len - consumed_len;
                        memmove(temp_buffer, temp_buffer + consumed_len, remaining);
                        temp_len = remaining;
                        temp_buffer[temp_len] = '\0';
                        wifi_detect_response_markers(temp_buffer, temp_len, &rx_flags);
                        
                        if (filesize == 0) break;
                    } 
                    else
                        break;
                }
            }
            
            if ((f_is_open && filesize == 0) || (rx_flags.has_closed && !rx_flags.has_ipd)) {
                break;
            }
        }
    }
    
    if (f_is_open) {
        f_close(&file);
    }
    
    return (filesize == 0 && f_is_open) ? 0 : -1;
}

/* Receive messages from client until socket is closed or timeout */
int wifi_receive_msg(void)
{
    uint8_t temp_buffer[RX_BUFFER_SIZE];
    uint32_t temp_len = 0;
    
    wifi_reset_rx_state();
    
    uint32_t timeout_ms = 30000;
    uint32_t start_time = HAL_GetTick();
    uint32_t timeout = start_time + timeout_ms;
    
    while (HAL_GetTick() < timeout) {
        size_t available_space = sizeof(temp_buffer) - temp_len - 1;
        if (available_space == 0) {
            temp_len = 0;
            available_space = sizeof(temp_buffer) - 1;
        }

        uint32_t remaining_ms = timeout - HAL_GetTick();
        if (remaining_ms > timeout_ms) break;

        size_t received = xStreamBufferReceive(wifi_rx_stream, temp_buffer + temp_len, available_space, pdMS_TO_TICKS(remaining_ms));
        if (received > 0) {
            temp_len += received;
            temp_buffer[temp_len] = '\0';
            
            WIFI_RESPONSE_FLAGS rx_flags;
            wifi_detect_response_markers(temp_buffer, temp_len, &rx_flags);
            
            kernel_printf("%s", (char *)(temp_buffer + temp_len - received));
            
            if (rx_flags.has_closed) {
                return 0;
            }
            if (rx_flags.has_error || rx_flags.has_fail) {
                return -1;
            }
        }
    }
    return -1;
}
