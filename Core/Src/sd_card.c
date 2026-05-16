#include <string.h>

#include "FreeRTOS.h"
#include "ff.h"

#include "sd_card.h"

static FATFS fs;
static uint8_t mounted;

int sd_init(void)
{
    FRESULT res = f_mount(&fs, "", 1);
    if (res != FR_OK)
        return -1;
    mounted = 1;
    return 0;
}

int sd_read_file(const char *path, uint8_t **buf, uint32_t *size)
{
    if (!mounted || !path || !buf || !size)
        return -1;

    FILINFO fno;
    if (f_stat(path, &fno) != FR_OK)
        return -1;

    uint32_t file_size = fno.fsize;
    uint8_t *data = pvPortMalloc(file_size);
    if (!data)
        return -1;

    FIL file;
    if (f_open(&file, path, FA_READ) != FR_OK) {
        vPortFree(data);
        return -1;
    }

    UINT bytes_read;
    FRESULT res = f_read(&file, data, file_size, &bytes_read);
    f_close(&file);

    if (res != FR_OK || bytes_read != file_size) {
        vPortFree(data);
        return -1;
    }

    *buf = data;
    *size = file_size;
    return 0;
}

int sd_list_apps(void (*callback)(const char *name, uint32_t size))
{
    if (!mounted || !callback)
        return -1;

    DIR dir;
    FILINFO fno;

    if (f_opendir(&dir, "") != FR_OK)
        return -1;

    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != '\0') {
        if (fno.fattrib & (AM_DIR | AM_HID | AM_SYS))
            continue;
        if (fno.fname[0] == '.' || fno.fname[0] == '_')
            continue;
        const char *ext = strrchr(fno.fname, '.');
        if (ext && (strcmp(ext, ".o") == 0 || strcmp(ext, ".O") == 0))
            callback(fno.fname, fno.fsize);
    }

    f_closedir(&dir);
    return 0;
}
