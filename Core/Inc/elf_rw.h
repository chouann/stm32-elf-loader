#pragma once
#include "fatfs.h"
#include "File_Handling.h"
#include "elf.h"

#define ELF_BUFFER_SIZE 512 

typedef enum {
  ELF_ERROR_NONE = 0,
  ELF_ERROR_IO,
  ELF_ERROR_INVALID_ELF,
  ELF_ERROR_UNSUPPORTED,
}ELF_ErrorTypeDef;

typedef enum {
  BUFFER_OFFSET_NONE = 0,
  BUFFER_OFFSET_HALF,
  BUFFER_OFFSET_FULL,
}BUFFER_StateTypeDef;

typedef struct {
  uint8_t buff[ELF_BUFFER_SIZE];
  BUFFER_StateTypeDef state;
  uint32_t fptr;
}ELF_BufferTypeDef;

/* Function prototypes */
ELF_ErrorTypeDef ELF_Open(char *filename);
ELF_ErrorTypeDef ELF_ReadHeader(Elf32_Ehdr *header);
ELF_ErrorTypeDef ELF_ReadSectionHeader(uint32_t index, Elf32_Shdr *section);
ELF_ErrorTypeDef ELF_ReadSectionData(Elf32_Shdr *section, uint8_t *buffer, uint32_t size);
ELF_ErrorTypeDef ELF_Close(void);

/* Read complete ELF file for elf_loader_load() */
ELF_ErrorTypeDef ELF_ReadComplete(char *filename, uint8_t **elf_data, uint32_t *elf_size);

/* Write buffer directly to file */
ELF_ErrorTypeDef ELF_write(char *filename, uint8_t *buffer, uint32_t size);

/* Hexdump ELF file raw bytes */
ELF_ErrorTypeDef ELF_hexdump(char *filename, uint32_t max_bytes);

/* Inspect and print ELF file structure */
ELF_ErrorTypeDef ELF_inspect(char *filename);