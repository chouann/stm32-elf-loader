/**
  ***************************************************************************************************************
  ***************************************************************************************************************
  ***************************************************************************************************************

  File:		  File_Handling.c
  Author:     ControllersTech.com
  Updated:    10th JAN 2021

  ***************************************************************************************************************
  Copyright (C) 2017 ControllersTech.com

  This is a free software under the GNU license, you can redistribute it and/or modify it under the terms
  of the GNU General Public License version 3 as published by the Free Software Foundation.
  This software library is shared with public for educational purposes, without WARRANTY and Author is not liable for any damages caused directly
  or indirectly by this software, read more about this on the GNU General Public License.

  ***************************************************************************************************************
*/

#include "File_Handling.h"
#include "stm32f4xx_hal.h"
#include "myprintf.h"
#include "elf_rw.h"
//#include "usb_host.h"

extern FILELIST_FileTypeDef FileList;
//extern ApplicationTypeDef Appli_state;

//extern char USBHPath[4];   /* USBH logical drive path */
//extern FATFS USBHFatFS;    /* File system object for USBH logical drive */
//extern FIL USBHFile;       /* File object for USBH */

//FILINFO USBHfno;
FATFS fs;
FRESULT fresult;  // result
//UINT br, bw;  // File read/write count

//USBH_HandleTypeDef hUSBHost;
uint16_t NumObs = 0;

/**
  * @brief  Validates if file has valid ELF header
  * @param  filename: Name of file to validate
  * @retval 1 if valid ELF, 0 otherwise
  */
static uint8_t IsValidELF(char *filename)
{
    FIL file;
    uint8_t elf_magic[4];
    uint32_t bytesread;
    FRESULT res;
    
    res = f_open(&file, filename, FA_READ);
    if(res != FR_OK)
        return 0;
    
    /* Read first 4 bytes to check ELF magic number */
    res = f_read(&file, elf_magic, 4, (void *)&bytesread);
    f_close(&file);
    
    if(res != FR_OK || bytesread != 4)
        return 0;
    
    /* Check for ELF magic: 0x7f 'E' 'L' 'F' */
    if(elf_magic[0] == 0x7f && elf_magic[1] == 'E' && 
       elf_magic[2] == 'L' && elf_magic[3] == 'F')
    {
        return 1;
    }
    
    return 0;
}

FRESULT ELF_StorageParse(void)
{
  FRESULT res = FR_OK;
  FILINFO fno;
  DIR dir;
  char *fn;

  myprintf("Parsing SD card for ELF files...\n");

  res = f_opendir(&dir, "");
  if (res != FR_OK) {
      myprintf("Failed to open root directory! Error: %d\n", res);
      return res;
  }
  FileList.ptr = 0;

  if(res == FR_OK)
  {
    while(1)
    {
      res = f_readdir(&dir, &fno);
      if(res != FR_OK || fno.fname[0] == 0)
      {
        break;
      }
      if(fno.fname[0] == '.')
      {
        continue;
      }

      fn = fno.fname;
      myprintf("Found file: %s (dir=%d)\n", fn, (fno.fattrib & AM_DIR) ? 1 : 0);

      if(FileList.ptr < FILEMGR_LIST_DEPDTH)
      {
        if((fno.fattrib & AM_DIR) == 0)
        {
          /* Check if filename contains '.o' extension and validate ELF header */
          if((strstr(fn, ".o")) || (strstr(fn, ".O")))
          {
            if(IsValidELF(fn))
            {
              strncpy((char *)FileList.file[FileList.ptr].name, (char *)fn, FILEMGR_FILE_NAME_SIZE);
              FileList.file[FileList.ptr].type = FILETYPE_ELF;
              myprintf("  -> Recording as valid ELF: %s\n", fn);
              FileList.ptr++;
            }
            else
            {
              myprintf("  -> Invalid ELF (bad magic): %s\n", fn);
            }
          }
        }
      }
    }
  }
  NumObs = FileList.ptr;
  f_closedir(&dir);
  myprintf("Total valid ELF files found: %d\n", NumObs);
  return res;
}

uint16_t ELF_GetelfObjectNumber(void)
{
	if (ELF_StorageParse() == FR_OK) return NumObs;
	else return -1;
}

void Mount_SD (void)
{
	fresult = f_mount(&fs, "", 1);

	if (fresult != FR_OK) {
	    myprintf("SD card mount failed with error code: %d\n", fresult);
	} else {
	    myprintf("SD card mounted Successfully.!\n");
	}
}

void Unmount_SD (void)
{
	fresult = f_mount(NULL, "", 1);

	if (fresult != FR_OK) {
		myprintf("SD card unmount failed with error code: %d\n", fresult);
	} else {
		myprintf("SD card unmounted Successfully.!\n");
	}
}
