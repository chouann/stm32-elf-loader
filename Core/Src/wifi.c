#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "ff.h"
#include "semphr.h"
#include "stream_buffer.h"

#include "app_config.h"
#include "app_registry.h"
#include "elf_loader.h"
#include "kernel_api.h"
#include "sd_card.h"
#include "task_manager.h"
#include "wifi.h"

#define RX_BUFFER_SIZE 512
#define WIFI_RX_STREAM_SIZE (RX_BUFFER_SIZE * 4)
#define WIFI_MAX_FILE_SIZE APP_MAX_FILE_SIZE
#define WIFI_CONNECT_TIMEOUT_MS 30000

typedef struct {
    uint8_t ok;
    uint8_t fail;
    uint8_t error;
    uint8_t ipd;
    uint8_t closed;
    uint8_t fname;
    uint8_t fsize;
    uint8_t cwjap;
    uint8_t prompt;
} wifi_resp_t;

extern UART_HandleTypeDef huart3;
extern DMA_HandleTypeDef hdma_usart3_rx;

static SemaphoreHandle_t wifi_uart_mutex;
static StreamBufferHandle_t wifi_rx_stream;
static uint8_t dma_tmp_buffer[RX_BUFFER_SIZE];
/* Data lost because the stream buffer was full; cleared on reset */
static volatile uint8_t wifi_rx_overflow;
/* DMA reception is dead; cleared only by a successful restart */
static volatile uint8_t wifi_dma_dead;

static void wifi_uart_start_rx(void)
{
    if (HAL_UARTEx_ReceiveToIdle_DMA(&huart3, dma_tmp_buffer,
                                     sizeof(dma_tmp_buffer)) != HAL_OK) {
        wifi_dma_dead = 1;
        return;
    }
    wifi_dma_dead = 0;
    /* Half-transfer interrupt would double-trigger on large packets */
    __HAL_DMA_DISABLE_IT(&hdma_usart3_rx, DMA_IT_HT);
}

void wifi_uart_resume_rx(void)
{
    wifi_uart_start_rx();
}

void wifi_start_rx(void)
{
    if (wifi_uart_mutex && wifi_rx_stream)
        wifi_uart_start_rx();
}

void wifi_handle_uart_rx_event(uint16_t size)
{
    if (!wifi_rx_stream)
        return;
    BaseType_t woken = pdFALSE;
    size_t sent =
        xStreamBufferSendFromISR(wifi_rx_stream, dma_tmp_buffer, size, &woken);
    if (sent != size)
        wifi_rx_overflow = 1;
    wifi_uart_start_rx();
    portYIELD_FROM_ISR(woken);
}

int wifi_dma_init(void)
{
    wifi_uart_mutex = xSemaphoreCreateMutex();
    wifi_rx_stream = xStreamBufferCreate(WIFI_RX_STREAM_SIZE, 1);
    if (!wifi_uart_mutex || !wifi_rx_stream) {
        if (wifi_uart_mutex)
            vSemaphoreDelete(wifi_uart_mutex);
        if (wifi_rx_stream)
            vStreamBufferDelete(wifi_rx_stream);
        wifi_uart_mutex = NULL;
        wifi_rx_stream = NULL;
        return -1;
    }
    return 0;
}

static void wifi_reset_rx(void)
{
    if (wifi_rx_stream)
        xStreamBufferReset(wifi_rx_stream);
    wifi_rx_overflow = 0;
}

static void wifi_detect_resp(const uint8_t *buf, uint32_t len, wifi_resp_t *r)
{
    memset(r, 0, sizeof(*r));
    if (!buf || len == 0)
        return;

    const char *s = (const char *) buf;
    r->ok = strstr(s, "OK") != NULL;
    r->fail = strstr(s, "FAIL") != NULL;
    r->error = strstr(s, "ERROR") != NULL;
    r->ipd = strstr(s, "+IPD,") != NULL;
    r->closed = strstr(s, "CLOSED") != NULL;
    r->fname = strstr(s, "FNAME") != NULL;
    r->fsize = strstr(s, "FSIZE") != NULL;
    r->cwjap = strstr(s, "+CWJAP:") != NULL;
    r->prompt = strchr(s, '>') != NULL;
}

typedef enum {
    IPD_OK = 0,
    IPD_INCOMPLETE, /* keep receiving */
    IPD_MALFORMED,  /* abort transfer */
} ipd_status_t;

/* Parse "+IPD,<id>,<len>:<data>".  conn_id_out may be NULL.
 * Precondition: buf must be NUL-terminated at buf[buf_len] (the str
 * functions below rely on it); every caller writes that terminator. */
static ipd_status_t ipd_parse_data(const char *buf,
                                   uint32_t buf_len,
                                   int *conn_id_out,
                                   const char **data_out,
                                   uint32_t *data_len_out,
                                   uint32_t *consumed_out)
{
    const char *ipd = strstr(buf, "+IPD,");
    if (!ipd)
        return IPD_INCOMPLETE;
    const char *colon = strchr(ipd, ':');
    if (!colon)
        return IPD_INCOMPLETE;

    /* ESP8266 multi-connection ids are 0..4 */
    char *end = NULL;
    uint32_t conn_id = strtoul(ipd + 5, &end, 10);
    if (end == ipd + 5 || *end != ',' || conn_id > 4)
        return IPD_MALFORMED;

    const char *len_str = end + 1;
    uint32_t expected = strtoul(len_str, &end, 10);
    if (end != colon || expected == 0 || expected > RX_BUFFER_SIZE)
        return IPD_MALFORMED;

    const char *data = colon + 1;
    uint32_t available = buf_len - (data - buf);

    if (available < expected)
        return IPD_INCOMPLETE;

    if (conn_id_out)
        *conn_id_out = (int) conn_id;
    *data_out = data;
    *data_len_out = expected;
    *consumed_out = (data - buf) + expected;
    return IPD_OK;
}

static void buf_shift(uint8_t *buf, uint32_t *len, uint32_t consumed)
{
    uint32_t remaining = *len - consumed;
    memmove(buf, buf + consumed, remaining);
    *len = remaining;
    buf[*len] = '\0';
}

/* Accept only a plain "name.o" basename: alphanumeric, '.', '_', '-',
 * no path separators, must end with ".o". */
static int filename_is_valid(const char *name)
{
    size_t n = strlen(name);
    if (n < 3 || name[0] == '.')
        return 0;
    if (strcmp(name + n - 2, ".o") != 0)
        return 0;
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

/* Wait until token appears in the RX stream or timeout.  Uses a rolling
 * window so the token is found no matter how much noise precedes it. */
static int wifi_wait_token(const char *token, uint32_t timeout_ms)
{
    uint8_t buf[64] = {0};
    uint32_t pos = 0;
    size_t keep = strlen(token) - 1;
    uint32_t deadline = HAL_GetTick() + timeout_ms;

    while (HAL_GetTick() < deadline) {
        if (wifi_dma_dead)
            return -1;
        if (pos >= sizeof(buf) - 1) {
            /* Keep only the tail that could still match across reads */
            memmove(buf, buf + pos - keep, keep);
            pos = keep;
        }

        size_t n =
            xStreamBufferReceive(wifi_rx_stream, buf + pos,
                                 sizeof(buf) - pos - 1, pdMS_TO_TICKS(100));
        if (n > 0) {
            pos += n;
            buf[pos] = '\0';
            if (strstr((char *) buf, token))
                return 0;
        }
    }
    return -1;
}

static int wifi_send_data(int conn_id, const char *data, uint32_t len)
{
    if (!wifi_uart_mutex || !wifi_rx_stream)
        return -1;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d,%lu\r\n", conn_id,
             (unsigned long) len);

    /* Protocol assumption: the peer waits for our ACK before sending
     * more data, so nothing of value is in the RX stream when a send
     * starts and it is safe to clear. */
    wifi_reset_rx();
    xSemaphoreTake(wifi_uart_mutex, portMAX_DELAY);
    HAL_StatusTypeDef tx =
        HAL_UART_Transmit(&huart3, (uint8_t *) cmd, strlen(cmd), 100);
    xSemaphoreGive(wifi_uart_mutex);
    if (tx != HAL_OK)
        return -1;

    if (wifi_wait_token(">", 2000) != 0)
        return -1;

    xSemaphoreTake(wifi_uart_mutex, portMAX_DELAY);
    tx = HAL_UART_Transmit(&huart3, (uint8_t *) data, len, 100);
    xSemaphoreGive(wifi_uart_mutex);
    if (tx != HAL_OK)
        return -1;

    return wifi_wait_token("SEND OK", 2000);
}

static int wifi_send_cmd(const char *cmd, uint32_t timeout_ms)
{
    if (!cmd || !wifi_uart_mutex || !wifi_rx_stream)
        return -1;

    uint8_t temp[RX_BUFFER_SIZE * 2];
    uint32_t len = 0;

    wifi_reset_rx();
    xSemaphoreTake(wifi_uart_mutex, portMAX_DELAY);
    HAL_StatusTypeDef tx =
        HAL_UART_Transmit(&huart3, (uint8_t *) cmd, strlen(cmd), 100);
    xSemaphoreGive(wifi_uart_mutex);
    if (tx != HAL_OK)
        return -1;

    uint32_t deadline = HAL_GetTick() + timeout_ms;

    while (HAL_GetTick() < deadline) {
        if (wifi_dma_dead) {
            kernel_printf("[wifi] RX dead\r\n");
            return -1;
        }
        size_t space = sizeof(temp) - len - 1;
        if (space == 0) {
            kernel_printf("[wifi] response too large\r\n");
            return -1;
        }

        uint32_t remaining = deadline - HAL_GetTick();
        if (remaining > timeout_ms)
            break;

        size_t n = xStreamBufferReceive(wifi_rx_stream, temp + len, space,
                                        pdMS_TO_TICKS(remaining));
        if (n > 0) {
            len += n;
            temp[len] = '\0';

            wifi_resp_t r;
            wifi_detect_resp(temp, len, &r);
            if (r.ok || r.fail || r.error || r.prompt || r.cwjap)
                break;
        }
    }

    if (len > 0) {
        kernel_printf("%s", (char *) temp);
        wifi_resp_t r;
        wifi_detect_resp(temp, len, &r);
        if (r.ok || r.prompt)
            return 0;
        if (r.cwjap && strstr((char *) temp, "+CWJAP:0"))
            return 0;
    }
    return -1;
}

int wifi_init(const char *ssid, const char *pass)
{
    char cmd[128];

    if (!ssid || !pass) {
        kernel_printf("[wifi] usage: wifi init <SSID> <PASS>\r\n");
        return -1;
    }

    kernel_printf("[wifi] connecting to %s\r\n", ssid);

    if (wifi_send_cmd("AT\r\n", 1000) != 0)
        return -1;
    if (wifi_send_cmd("AT+CWMODE=1\r\n", 1000) != 0)
        return -1;

    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pass);
    if (wifi_send_cmd(cmd, WIFI_CONNECT_TIMEOUT_MS) != 0)
        return -1;

    if (wifi_send_cmd("AT+CIFSR\r\n", 2000) != 0)
        return -1;
    if (wifi_send_cmd("AT+CIPMUX=1\r\n", 2000) != 0)
        return -1;

    snprintf(cmd, sizeof(cmd), "AT+CIPSERVER=1,%d\r\n", WIFI_SERVER_PORT);
    if (wifi_send_cmd(cmd, 2000) != 0)
        return -1;

    return 0;
}

/* Uploads are written to a temp file and renamed into place only after
 * a successful close, so a failed transfer normally leaves an existing
 * app of the same name untouched.  Best effort, not atomic: the old
 * file is unlinked just before the rename, so a failure (or power
 * loss) in that narrow window can still lose the old version. */
#define UPLOAD_TMP_NAME "_upload.tmp"

/* Close and remove a partially written upload so an aborted transfer
 * does not leave a corrupt .o on the SD card. */
static void wifi_discard_file(FIL *file, const char *filename)
{
    f_close(file);
    if (f_unlink(filename) != FR_OK)
        kernel_printf("[wifi] Failed to remove %s\r\n", filename);
}

int wifi_receive_file(void)
{
    if (!wifi_uart_mutex || !wifi_rx_stream)
        return -1;

    char filename[32] = "";
    uint32_t filesize = 0;
    uint32_t total_size = 0;
    int conn_id = 0;
    FIL file;
    uint8_t file_open = 0;

    uint8_t temp[RX_BUFFER_SIZE * 2];
    uint32_t len = 0;

    wifi_reset_rx();

    uint32_t timeout_ms = 60000;
    uint32_t deadline = HAL_GetTick() + timeout_ms;

    while (HAL_GetTick() < deadline) {
        size_t space = sizeof(temp) - len - 1;
        if (space == 0) {
            kernel_printf("[wifi] Frame too large, aborting\r\n");
            if (file_open)
                wifi_discard_file(&file, UPLOAD_TMP_NAME);
            return -1;
        }

        uint32_t remaining = deadline - HAL_GetTick();
        if (remaining > timeout_ms)
            break;

        size_t n = xStreamBufferReceive(wifi_rx_stream, temp + len, space,
                                        pdMS_TO_TICKS(remaining));
        if (n == 0)
            continue;

        if (wifi_rx_overflow || wifi_dma_dead) {
            kernel_printf("[wifi] RX %s, aborting\r\n",
                          wifi_dma_dead ? "dead" : "overflow");
            if (file_open)
                wifi_discard_file(&file, UPLOAD_TMP_NAME);
            return -1;
        }

        len += n;
        temp[len] = '\0';

        wifi_resp_t r;
        wifi_detect_resp(temp, len, &r);

        if (!(r.ipd && !r.fname))
            kernel_printf("%s", (char *) (temp + len - n));

        if (!file_open) {
            /* Wait for the complete header IPD frame before parsing,
             * opening the file, or ACKing.  Partial frames keep
             * accumulating in temp. */
            const char *data;
            uint32_t data_len, consumed;
            ipd_status_t st = ipd_parse_data((char *) temp, len, &conn_id,
                                             &data, &data_len, &consumed);
            if (st == IPD_MALFORMED) {
                kernel_printf("[wifi] Malformed header frame\r\n");
                return -1;
            }
            if (st != IPD_OK)
                continue;

            /* Bounded copy of the payload so str functions stay inside.
             * A header longer than the buffer is rejected outright
             * rather than truncated and parsed. */
            char hdr[96];
            if (data_len >= sizeof(hdr)) {
                kernel_printf("\r\n[wifi] Header frame too large\r\n");
                return -1;
            }
            memcpy(hdr, data, data_len);
            hdr[data_len] = '\0';

            char *fp = strstr(hdr, "FNAME=");
            char *sp = strstr(hdr, "FSIZE=");
            if (!fp || !sp) {
                kernel_printf("\r\n[wifi] Missing FNAME/FSIZE header\r\n");
                return -1;
            }
            fp += 6;

            char *comma = strchr(fp, ',');
            if (!comma)
                return -1;
            int name_len = comma - fp;
            if (name_len <= 0 || name_len >= (int) sizeof(filename)) {
                kernel_printf("\r\n[wifi] Filename too long\r\n");
                return -1;
            }
            strncpy(filename, fp, name_len);
            filename[name_len] = '\0';
            if (!filename_is_valid(filename)) {
                kernel_printf("\r\n[wifi] Invalid filename: %s\r\n", filename);
                return -1;
            }

            char *end = NULL;
            filesize = strtoul(sp + 6, &end, 10);
            if (end == sp + 6) {
                kernel_printf("\r\n[wifi] Invalid file size\r\n");
                return -1;
            }
            /* The header frame must carry nothing but the header line:
             * file bytes packed into the same frame would be dropped
             * with the frame below, so reject them explicitly. */
            for (const char *p = end; p < hdr + data_len; p++) {
                if (*p != '\r' && *p != '\n' && *p != ' ') {
                    kernel_printf(
                        "\r\n[wifi] Header frame contains extra data\r\n");
                    return -1;
                }
            }
            total_size = filesize;
            kernel_printf("\r\n[wifi] Opening %s (%lu bytes)\r\n", filename,
                          (unsigned long) filesize);
            if (filesize == 0 || filesize > WIFI_MAX_FILE_SIZE) {
                kernel_printf("[wifi] Invalid file size\r\n");
                return -1;
            }

            if (f_open(&file, UPLOAD_TMP_NAME, FA_CREATE_ALWAYS | FA_WRITE) !=
                FR_OK) {
                kernel_printf("[wifi] Failed to open %s\r\n", filename);
                return -1;
            }
            file_open = 1;

            buf_shift(temp, &len, consumed);

            if (wifi_send_data(conn_id, "ACK", 3) != 0) {
                kernel_printf("[wifi] ACK failed, aborting\r\n");
                wifi_discard_file(&file, UPLOAD_TMP_NAME);
                return -1;
            }
            continue;
        }

        if (file_open && filesize > 0) {
            while (r.ipd) {
                const char *data;
                int chunk_conn_id;
                uint32_t data_len, consumed;
                ipd_status_t st =
                    ipd_parse_data((char *) temp, len, &chunk_conn_id, &data,
                                   &data_len, &consumed);
                if (st == IPD_MALFORMED) {
                    kernel_printf("[wifi] Malformed frame, aborting\r\n");
                    wifi_discard_file(&file, UPLOAD_TMP_NAME);
                    return -1;
                }
                if (st != IPD_OK)
                    break;

                if (chunk_conn_id != conn_id) {
                    kernel_printf("[wifi] Unexpected connection %d\r\n",
                                  chunk_conn_id);
                    wifi_discard_file(&file, UPLOAD_TMP_NAME);
                    return -1;
                }

                if (data_len > filesize) {
                    kernel_printf("[wifi] Data exceeds declared size\r\n");
                    wifi_discard_file(&file, UPLOAD_TMP_NAME);
                    return -1;
                }

                UINT written;
                FRESULT res = f_write(&file, data, data_len, (void *) &written);
                if (res != FR_OK || written != data_len) {
                    kernel_printf("[wifi] Write error\r\n");
                    wifi_discard_file(&file, UPLOAD_TMP_NAME);
                    return -1;
                }

                filesize -= written;

                buf_shift(temp, &len, consumed);
                r.ipd = strstr((char *) temp, "+IPD,") != NULL;

                if (wifi_send_data(conn_id, "ACK", 3) != 0) {
                    kernel_printf("[wifi] ACK failed, aborting\r\n");
                    wifi_discard_file(&file, UPLOAD_TMP_NAME);
                    return -1;
                }

                if (filesize == 0)
                    break;
            }

            /* Re-detect on what remains: a CLOSED that arrived in the
             * same batch as the final data frames would otherwise sit
             * unseen until the timeout. */
            wifi_detect_resp(temp, len, &r);
        }

        if ((file_open && filesize == 0) || (r.closed && !r.ipd))
            break;
    }

    if (file_open) {
        if (filesize == 0) {
            if (f_close(&file) != FR_OK) {
                /* The final flush may not have hit the card, so the
                 * file cannot be trusted; remove it. */
                kernel_printf("[wifi] Close failed, discarding %s\r\n",
                              filename);
                if (f_unlink(UPLOAD_TMP_NAME) != FR_OK)
                    kernel_printf("[wifi] Failed to remove %s\r\n",
                                  UPLOAD_TMP_NAME);
                return -1;
            }
            /* Replace any old version only now that the upload is
             * complete and closed; f_rename() fails if the target
             * still exists, so remove it first. */
            f_unlink(filename);
            if (f_rename(UPLOAD_TMP_NAME, filename) != FR_OK) {
                kernel_printf("[wifi] Failed to save %s\r\n", filename);
                f_unlink(UPLOAD_TMP_NAME);
                return -1;
            }
            kernel_printf("[wifi] Received %lu bytes\r\n",
                          (unsigned long) total_size);
            return 0;
        }
        /* Timed out mid-transfer: do not leave a partial .o */
        kernel_printf("[wifi] Incomplete transfer, discarding %s\r\n",
                      filename);
        wifi_discard_file(&file, UPLOAD_TMP_NAME);
    }

    return -1;
}

int wifi_receive_msg(void)
{
    if (!wifi_uart_mutex || !wifi_rx_stream)
        return -1;

    uint8_t temp[RX_BUFFER_SIZE];
    uint32_t len = 0;

    wifi_reset_rx();

    uint32_t timeout_ms = 30000;
    uint32_t deadline = HAL_GetTick() + timeout_ms;

    while (HAL_GetTick() < deadline) {
        size_t space = sizeof(temp) - len - 1;
        if (space == 0) {
            /* Clearing the buffer here would desync the IPD parser from
             * the rest of the oversized frame, so give up instead. */
            kernel_printf("[wifi] message too large, aborting\r\n");
            return -1;
        }

        uint32_t remaining = deadline - HAL_GetTick();
        if (remaining > timeout_ms)
            break;

        size_t n = xStreamBufferReceive(wifi_rx_stream, temp + len, space,
                                        pdMS_TO_TICKS(remaining));
        if (n == 0)
            continue;

        if (wifi_rx_overflow || wifi_dma_dead) {
            kernel_printf("[wifi] RX %s, aborting\r\n",
                          wifi_dma_dead ? "dead" : "overflow");
            return -1;
        }

        len += n;
        temp[len] = '\0';

        /* Print each +IPD payload as a clean message line */
        const char *data;
        uint32_t data_len, consumed;
        ipd_status_t st;
        while ((st = ipd_parse_data((char *) temp, len, NULL, &data, &data_len,
                                    &consumed)) == IPD_OK) {
            kernel_printf("[wifi] message: %.*s", (int) data_len, data);
            if (data_len == 0 || data[data_len - 1] != '\n')
                kernel_printf("\r\n");
            buf_shift(temp, &len, consumed);
        }
        if (st == IPD_MALFORMED) {
            kernel_printf("[wifi] Malformed frame\r\n");
            return -1;
        }

        /* Detect control responses only after the payloads have been
         * consumed, so message content (e.g. a message saying "FAIL"
         * or "CLOSED") is not mistaken for ESP status. */
        wifi_resp_t r;
        wifi_detect_resp(temp, len, &r);

        if (r.closed) {
            kernel_printf("[wifi] connection closed\r\n");
            return 0;
        }
        if (r.error || r.fail)
            return -1;
    }
    return -1;
}

/* ---- serve mode: persistent command channel ----------------------- */

/* Like wifi_wait_token but preserves data around the token instead of
 * discarding it.  When the next +IPD frame races with "SEND OK" on
 * the UART, the +IPD bytes end up in save_buf so the file-transfer
 * loop can still parse them. */
static int serve_at_wait(const char *token,
                         uint32_t timeout_ms,
                         uint8_t *save_buf,
                         uint32_t *save_len,
                         uint32_t save_cap)
{
    uint8_t buf[256];
    uint32_t pos = 0;
    size_t tlen = strlen(token);
    uint32_t deadline = HAL_GetTick() + timeout_ms;

    while (HAL_GetTick() < deadline) {
        if (wifi_dma_dead)
            return -1;
        if (pos >= sizeof(buf) - 1) {
            size_t keep = tlen > 1 ? tlen - 1 : 0;
            memmove(buf, buf + pos - keep, keep);
            pos = keep;
        }

        uint32_t rem = deadline - HAL_GetTick();
        if (rem > timeout_ms)
            break;
        size_t n = xStreamBufferReceive(wifi_rx_stream, buf + pos,
                                        sizeof(buf) - pos - 1,
                                        pdMS_TO_TICKS(rem < 100 ? rem : 100));
        if (n == 0)
            continue;
        pos += n;
        buf[pos] = '\0';

        char *found = strstr((char *) buf, token);
        if (!found)
            continue;

        /* Save non-token data (before and after) into save_buf.
         * AT echoes are harmless noise; ipd_parse_data skips them. */
        char *tail = found + tlen;
        while (*tail == '\r' || *tail == '\n')
            tail++;
        uint32_t before = (uint32_t) (found - (char *) buf);
        uint32_t after = pos - (uint32_t) (tail - (char *) buf);

        if (save_buf && save_len) {
            uint32_t room = save_cap - *save_len - 1;
            uint32_t sb = before < room ? before : room;
            if (sb > 0) {
                memcpy(save_buf + *save_len, buf, sb);
                *save_len += sb;
            }
            room = save_cap - *save_len - 1;
            uint32_t sa = after < room ? after : room;
            if (sa > 0) {
                memcpy(save_buf + *save_len, tail, sa);
                *save_len += sa;
            }
            save_buf[*save_len] = '\0';
        }
        return 0;
    }
    return -1;
}

/* Send data on a serve connection via AT+CIPSEND.  Clears accumulated
 * noise (CONNECT/CLOSED, old AT echoes) before sending so the prompt
 * search doesn't false-match on stale '>' bytes.  If save_buf is
 * non-NULL, any non-token data received during the SEND OK wait
 * (typically a racing +IPD frame) is preserved into save_buf. */
static int serve_send(int conn_id,
                      const char *data,
                      uint32_t len,
                      uint8_t *save_buf,
                      uint32_t *save_len)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d,%lu\r\n", conn_id,
             (unsigned long) len);

    /* Clear noise accumulated since the last AT command so strstr(">")
     * only matches the real prompt from this CIPSEND.  The host is
     * still waiting for our response, so no +IPD data is in flight. */
    wifi_reset_rx();

    xSemaphoreTake(wifi_uart_mutex, portMAX_DELAY);
    HAL_StatusTypeDef tx =
        HAL_UART_Transmit(&huart3, (uint8_t *) cmd, strlen(cmd), 100);
    xSemaphoreGive(wifi_uart_mutex);
    if (tx != HAL_OK)
        return -1;

    if (serve_at_wait(">", 2000, NULL, NULL, 0) != 0)
        return -1;

    xSemaphoreTake(wifi_uart_mutex, portMAX_DELAY);
    tx = HAL_UART_Transmit(&huart3, (uint8_t *) data, len, 100);
    xSemaphoreGive(wifi_uart_mutex);
    if (tx != HAL_OK)
        return -1;

    uint32_t cap = save_buf ? RX_BUFFER_SIZE * 2 : 0;
    return serve_at_wait("SEND OK", 2000, save_buf, save_len, cap);
}

#define SERVE_LIT(cid, s) serve_send(cid, s, sizeof(s) - 1, NULL, NULL)

static int serve_reply(int conn_id, const char *data, uint32_t len)
{
    return serve_send(conn_id, data, len, NULL, NULL);
}

static int serve_ack(int conn_id, uint8_t *temp, uint32_t *temp_len)
{
    return serve_send(conn_id, "ACK", 3, temp, temp_len);
}

static char serve_resp[1024];
static int serve_resp_pos;

#define SERVE_RESP_RESERVE 5 /* room for "END\n" + NUL */

static void serve_append(const char *fmt, ...)
{
    int limit = (int) sizeof(serve_resp) - SERVE_RESP_RESERVE;
    if (serve_resp_pos >= limit)
        return;
    va_list ap;
    va_start(ap, fmt);
    int n =
        vsnprintf(serve_resp + serve_resp_pos, limit - serve_resp_pos, fmt, ap);
    va_end(ap);
    if (n > 0) {
        serve_resp_pos += n;
        if (serve_resp_pos > limit)
            serve_resp_pos = limit;
    }
}

static void serve_finish(int conn_id)
{
    int cap = (int) sizeof(serve_resp) - 1;
    int n = snprintf(serve_resp + serve_resp_pos, cap - serve_resp_pos + 1,
                     "END\n");
    if (n > 0)
        serve_resp_pos += n;
    if (serve_resp_pos > cap)
        serve_resp_pos = cap;
    serve_reply(conn_id, serve_resp, serve_resp_pos);
}

static void serve_sd_cb(const char *name, uint32_t size)
{
    serve_append("%s %lu sd\n", name, (unsigned long) size);
}

static void serve_handle_list(int conn_id)
{
    serve_resp_pos = 0;

    const app_entry_t *apps = app_list();
    for (int i = 0; apps[i].name != NULL; i++)
        serve_append("%s %lu embedded\n", apps[i].name,
                     (unsigned long) apps[i].size);

    sd_list_apps(serve_sd_cb);
    serve_finish(conn_id);
}

static void serve_handle_ps(int conn_id)
{
    serve_resp_pos = 0;

    for (int i = 0; i < MAX_TASKS; i++) {
        task_info_t info;
        if (task_get_info(i, &info) == 0 && info.used) {
            uint32_t stack_total = TASK_STACK_SIZE * sizeof(StackType_t);
            uint32_t stack_used = stack_total - info.stack_free;
            serve_append("%d %s %lu %lu/%lu\n", info.id, info.name,
                         (unsigned long) info.memory_size,
                         (unsigned long) stack_used,
                         (unsigned long) stack_total);
        }
    }

    serve_finish(conn_id);
}

static void serve_handle_run(int conn_id, const char *name)
{
    const uint8_t *elf_data = NULL;
    uint32_t elf_size = 0;
    uint8_t *sd_buf = NULL;

    const app_entry_t *app = app_find(name);
    if (app) {
        elf_data = app->data;
        elf_size = app->size;
    } else {
        if (!filename_is_valid(name)) {
            SERVE_LIT(conn_id, "ERR invalid name\n");
            return;
        }
        if (sd_read_file(name, &sd_buf, &elf_size) != 0) {
            SERVE_LIT(conn_id, "ERR not found\n");
            return;
        }
        elf_data = sd_buf;
    }

    elf_load_result_t result;
    elf_status_t rc = elf_load(elf_data, elf_size, kernel_lookup, &result);
    vPortFree(sd_buf);

    if (rc != ELF_OK) {
        int n = snprintf(serve_resp, sizeof(serve_resp), "ERR %s\n",
                         elf_status_str(rc));
        serve_reply(conn_id, serve_resp, n);
        return;
    }

    int id = task_create(name, result.entry, result.cleanup, result.memory,
                         result.memory_size);
    if (id < 0) {
        SERVE_LIT(conn_id, "ERR task create failed\n");
        return;
    }

    int n = snprintf(serve_resp, sizeof(serve_resp), "OK %d\n", id);
    serve_reply(conn_id, serve_resp, n);
    kernel_printf("[serve] started %s as task %d\r\n", name, id);
}

static void serve_handle_kill(int conn_id, const char *id_str)
{
    char *end = NULL;
    long id = strtol(id_str, &end, 10);
    if (end == id_str || *end != '\0' || id < 0 || id >= MAX_TASKS) {
        SERVE_LIT(conn_id, "ERR invalid id\n");
        return;
    }

    if (task_kill((int) id) == 0) {
        SERVE_LIT(conn_id, "OK\n");
        kernel_printf("[serve] killed task %ld\r\n", id);
    } else {
        SERVE_LIT(conn_id, "ERR kill failed\n");
    }
}

static void serve_handle_rm(int conn_id, const char *name)
{
    if (!filename_is_valid(name)) {
        SERVE_LIT(conn_id, "ERR invalid name\n");
        return;
    }
    int res = sd_rm_file(name);
    if (res == 0) {
        SERVE_LIT(conn_id, "OK\n");
        kernel_printf("[serve] removed %s\r\n", name);
    } else {
        SERVE_LIT(conn_id, "ERR rm failed\n");
    }
}

static int serve_handle_put(int conn_id,
                            const char *hdr_payload,
                            uint8_t *temp,
                            uint32_t *len_ptr)
{
    char hdr[96];
    size_t plen = strlen(hdr_payload);
    if (plen >= sizeof(hdr)) {
        SERVE_LIT(conn_id, "ERR header too large\n");
        return -1;
    }
    memcpy(hdr, hdr_payload, plen + 1);

    char *fp = strstr(hdr, "FNAME=");
    char *sp = strstr(hdr, "FSIZE=");
    if (!fp || !sp) {
        SERVE_LIT(conn_id, "ERR missing FNAME/FSIZE\n");
        return -1;
    }
    fp += 6;

    char filename[32];
    char *comma = strchr(fp, ',');
    if (!comma) {
        SERVE_LIT(conn_id, "ERR bad header\n");
        return -1;
    }
    int name_len = comma - fp;
    if (name_len <= 0 || name_len >= (int) sizeof(filename)) {
        SERVE_LIT(conn_id, "ERR filename too long\n");
        return -1;
    }
    strncpy(filename, fp, name_len);
    filename[name_len] = '\0';
    if (!filename_is_valid(filename)) {
        SERVE_LIT(conn_id, "ERR invalid filename\n");
        return -1;
    }

    char *end = NULL;
    uint32_t filesize = strtoul(sp + 6, &end, 10);
    if (end == sp + 6 || filesize == 0 || filesize > WIFI_MAX_FILE_SIZE) {
        SERVE_LIT(conn_id, "ERR invalid size\n");
        return -1;
    }

    FIL file;
    if (f_open(&file, UPLOAD_TMP_NAME, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
        SERVE_LIT(conn_id, "ERR open failed\n");
        return -1;
    }

    kernel_printf("[serve] receiving %s (%lu bytes)\r\n", filename,
                  (unsigned long) filesize);

    if (serve_ack(conn_id, temp, len_ptr) != 0) {
        wifi_discard_file(&file, UPLOAD_TMP_NAME);
        return -1;
    }

    uint32_t timeout_ms = 60000;
    uint32_t deadline = HAL_GetTick() + timeout_ms;

    while (filesize > 0 && HAL_GetTick() < deadline) {
        if (wifi_rx_overflow || wifi_dma_dead) {
            wifi_discard_file(&file, UPLOAD_TMP_NAME);
            return -1;
        }

        size_t space = RX_BUFFER_SIZE * 2 - *len_ptr - 1;
        if (space == 0) {
            wifi_discard_file(&file, UPLOAD_TMP_NAME);
            return -1;
        }

        uint32_t rem = deadline - HAL_GetTick();
        if (rem > timeout_ms)
            break;

        size_t n = xStreamBufferReceive(wifi_rx_stream, temp + *len_ptr, space,
                                        pdMS_TO_TICKS(rem < 500 ? rem : 500));
        if (n == 0)
            continue;

        *len_ptr += n;
        temp[*len_ptr] = '\0';

        const char *data;
        int chunk_conn_id;
        uint32_t data_len, consumed;
        ipd_status_t st;
        while ((st = ipd_parse_data((char *) temp, *len_ptr, &chunk_conn_id,
                                    &data, &data_len, &consumed)) == IPD_OK) {
            if (chunk_conn_id != conn_id || data_len > filesize) {
                wifi_discard_file(&file, UPLOAD_TMP_NAME);
                return -1;
            }

            UINT written;
            if (f_write(&file, data, data_len, (void *) &written) != FR_OK ||
                written != data_len) {
                wifi_discard_file(&file, UPLOAD_TMP_NAME);
                return -1;
            }

            filesize -= written;
            buf_shift(temp, len_ptr, consumed);

            if (serve_ack(conn_id, temp, len_ptr) != 0) {
                wifi_discard_file(&file, UPLOAD_TMP_NAME);
                return -1;
            }

            if (filesize == 0)
                break;
        }
        if (st == IPD_MALFORMED) {
            wifi_discard_file(&file, UPLOAD_TMP_NAME);
            return -1;
        }
    }

    if (filesize != 0) {
        kernel_printf("[serve] incomplete transfer\r\n");
        wifi_discard_file(&file, UPLOAD_TMP_NAME);
        return -1;
    }

    if (f_close(&file) != FR_OK) {
        f_unlink(UPLOAD_TMP_NAME);
        return -1;
    }

    f_unlink(filename);
    if (f_rename(UPLOAD_TMP_NAME, filename) != FR_OK) {
        f_unlink(UPLOAD_TMP_NAME);
        return -1;
    }

    kernel_printf("[serve] saved %s\r\n", filename);
    return 0;
}

int wifi_serve(void)
{
    if (!wifi_uart_mutex || !wifi_rx_stream)
        return -1;

    wifi_reset_rx();
    /* Drain any leftover \r\n from the shell readline that launched us,
     * otherwise the first nonblock check would exit immediately. */
    while (kernel_getchar_nonblock() >= 0) {
    }
    /* Let in-flight DMA data settle, then clear again so the first
     * request doesn't see stale AT responses from wifi_init. */
    vTaskDelay(pdMS_TO_TICKS(100));
    wifi_reset_rx();
    kernel_printf("[wifi] serving on :%d (press any key to stop)\r\n",
                  WIFI_SERVER_PORT);

    uint8_t temp[RX_BUFFER_SIZE * 2];
    uint32_t len = 0;

    while (1) {
        /* Try to parse a complete IPD from what is already buffered
         * before blocking on the stream; a prior handler (e.g. a
         * failed PUT retry) may have left the next request in temp. */
        int conn_id;
        const char *data;
        uint32_t data_len, consumed;
        ipd_status_t st = (len > 0)
                              ? ipd_parse_data((char *) temp, len, &conn_id,
                                               &data, &data_len, &consumed)
                              : IPD_INCOMPLETE;

        if (st == IPD_MALFORMED) {
            len = 0;
            continue;
        }

        if (st == IPD_INCOMPLETE) {
            if (kernel_getchar_nonblock() >= 0)
                break;

            if (wifi_dma_dead) {
                kernel_printf("[wifi] RX dead\r\n");
                return -1;
            }

            size_t space = sizeof(temp) - len - 1;
            if (space == 0)
                len = 0;

            size_t n =
                xStreamBufferReceive(wifi_rx_stream, temp + len,
                                     space ? space : 1, pdMS_TO_TICKS(100));
            if (n == 0)
                continue;

            len += n;
            temp[len] = '\0';
            continue;
        }

        /* IPD_OK: dispatch the command */
        char cmd[96];
        uint32_t cmd_len =
            data_len < sizeof(cmd) - 1 ? data_len : sizeof(cmd) - 1;
        memcpy(cmd, data, cmd_len);
        cmd[cmd_len] = '\0';
        buf_shift(temp, &len, consumed);

        while (cmd_len > 0 &&
               (cmd[cmd_len - 1] == '\n' || cmd[cmd_len - 1] == '\r'))
            cmd[--cmd_len] = '\0';

        kernel_printf("[serve] %d: %s\r\n", conn_id, cmd);

        if (strcmp(cmd, "LIST") == 0) {
            serve_handle_list(conn_id);
        } else if (strcmp(cmd, "PS") == 0) {
            serve_handle_ps(conn_id);
        } else if (strncmp(cmd, "RUN ", 4) == 0) {
            serve_handle_run(conn_id, cmd + 4);
        } else if (strncmp(cmd, "KILL ", 5) == 0) {
            serve_handle_kill(conn_id, cmd + 5);
        } else if (strncmp(cmd, "RM ", 3) == 0) {
            serve_handle_rm(conn_id, cmd + 3);
        } else if (strstr(cmd, "FNAME=") != NULL) {
            if (serve_handle_put(conn_id, cmd, temp, &len) == 0)
                kernel_printf("[serve] upload OK\r\n");
            else
                kernel_printf("[serve] upload failed\r\n");
        } else {
            SERVE_LIT(conn_id, "ERR unknown\n");
        }
    }

    kernel_printf("[wifi] serve stopped\r\n");
    return 0;
}
