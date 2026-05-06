#pragma once

#include <stdint.h>

#define ELF_MAGIC \
    "\x7f"        \
    "ELF"
#define ELF_MAGIC_SIZE 4

#define ELFCLASS32 1
#define ELFDATA2LSB 1

#define ET_REL 1
#define EM_ARM 0x28

#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_NOBITS 8
#define SHT_REL 9

#define SHF_WRITE 0x1
#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4

#define STB_LOCAL 0
#define STB_GLOBAL 1

#define ELF32_ST_BIND(i) ((i) >> 4)
#define ELF32_ST_TYPE(i) ((i) & 0xf)

#define SHN_UNDEF 0
#define SHN_ABS 0xFFF1

#define ELF32_R_SYM(i) ((i) >> 8)
#define ELF32_R_TYPE(i) ((i) & 0xFF)

#define R_ARM_NONE 0
#define R_ARM_ABS32 2
#define R_ARM_THM_CALL 10
#define R_ARM_THM_JUMP24 30
#define R_ARM_THM_MOVW_ABS_NC 47
#define R_ARM_THM_MOVT_ABS 48

typedef struct {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
} Elf32_Shdr;

typedef struct {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
} Elf32_Sym;

typedef struct {
    uint32_t r_offset;
    uint32_t r_info;
} Elf32_Rel;

_Static_assert(sizeof(Elf32_Ehdr) == 52, "ELF header must be 52 bytes");
_Static_assert(sizeof(Elf32_Shdr) == 40, "Section header must be 40 bytes");
_Static_assert(sizeof(Elf32_Sym) == 16, "Symbol entry must be 16 bytes");
_Static_assert(sizeof(Elf32_Rel) == 8, "Relocation entry must be 8 bytes");
