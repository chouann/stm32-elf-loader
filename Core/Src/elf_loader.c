#include <assert.h>
#include <string.h>

#include "stm32f4xx.h"

#include "elf.h"
#include "elf_loader.h"
#include "kernel_api.h"

#define MAX_SECTIONS 32
#define SECTION_NOT_LOADED 0xFFFFFFFF

typedef struct {
    const Elf32_Ehdr *ehdr;
    const Elf32_Shdr *shdrs;
    const char *shstrtab;
    const uint8_t *elf_data;
    uint32_t elf_size;
    uint8_t *loaded_mem;
    uint32_t loaded_size;
    uint32_t base_addr;
    uint32_t section_offsets[MAX_SECTIONS];
} LoaderState_t;

static int loader_validate(LoaderState_t *s)
{
    if (s->elf_size < sizeof(Elf32_Ehdr))
        return -1;

    const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *) s->elf_data;

    if (memcmp(ehdr->e_ident, ELF_MAGIC, ELF_MAGIC_SIZE) != 0)
        return -1;
    if (ehdr->e_ident[4] != ELFCLASS32)
        return -1;
    if (ehdr->e_ident[5] != ELFDATA2LSB)
        return -1;
    if (ehdr->e_type != ET_REL)
        return -1;
    if (ehdr->e_machine != EM_ARM)
        return -1;

    s->ehdr = ehdr;
    s->shdrs = (const Elf32_Shdr *) (s->elf_data + ehdr->e_shoff);
    s->shstrtab =
        (const char *) (s->elf_data + s->shdrs[ehdr->e_shstrndx].sh_offset);
    return 0;
}

static int loader_load_sections(LoaderState_t *s)
{
    uint32_t total = 0;

    for (int i = 0; i < s->ehdr->e_shnum && i < MAX_SECTIONS; i++) {
        s->section_offsets[i] = SECTION_NOT_LOADED;

        if (!(s->shdrs[i].sh_flags & SHF_ALLOC))
            continue;

        uint32_t align = s->shdrs[i].sh_addralign;
        if (align > 1)
            total = (total + align - 1) & ~(align - 1);

        s->section_offsets[i] = total;
        total += s->shdrs[i].sh_size;
    }

    s->loaded_mem = pvPortMalloc(total);
    if (!s->loaded_mem)
        return -1;

    s->loaded_size = total;
    s->base_addr = (uint32_t) s->loaded_mem;
    memset(s->loaded_mem, 0, total);

    for (int i = 0; i < s->ehdr->e_shnum && i < MAX_SECTIONS; i++) {
        if (s->section_offsets[i] == SECTION_NOT_LOADED)
            continue;

        uint32_t offset = s->section_offsets[i];

        if (s->shdrs[i].sh_type != SHT_NOBITS) {
            memcpy(s->loaded_mem + offset, s->elf_data + s->shdrs[i].sh_offset,
                   s->shdrs[i].sh_size);
        }
    }

    return 0;
}

static int loader_resolve_and_relocate(LoaderState_t *s)
{
    for (int i = 0; i < s->ehdr->e_shnum; i++) {
        if (s->shdrs[i].sh_type != SHT_REL)
            continue;

        uint32_t target_sec = s->shdrs[i].sh_info;
        if (target_sec >= MAX_SECTIONS ||
            s->section_offsets[target_sec] == SECTION_NOT_LOADED)
            continue;

        const Elf32_Rel *rels =
            (const Elf32_Rel *) (s->elf_data + s->shdrs[i].sh_offset);
        uint32_t rel_count = s->shdrs[i].sh_size / sizeof(Elf32_Rel);

        const Elf32_Shdr *symtab_shdr = &s->shdrs[s->shdrs[i].sh_link];
        const Elf32_Sym *syms =
            (const Elf32_Sym *) (s->elf_data + symtab_shdr->sh_offset);
        uint32_t sym_count = symtab_shdr->sh_size / sizeof(Elf32_Sym);
        const Elf32_Shdr *strtab_shdr = &s->shdrs[symtab_shdr->sh_link];
        const char *strtab =
            (const char *) (s->elf_data + strtab_shdr->sh_offset);

        uint32_t *sym_addrs = pvPortMalloc(sym_count * sizeof(uint32_t));
        if (!sym_addrs)
            return -1;
        memset(sym_addrs, 0, sym_count * sizeof(uint32_t));

        for (uint32_t j = 1; j < sym_count; j++) {
            if (syms[j].st_shndx == SHN_UNDEF) {
                const char *name = strtab + syms[j].st_name;
                uint32_t addr = kernel_symbol_lookup(name);
                if (addr == 0) {
                    vPortFree(sym_addrs);
                    return -1;
                }
                sym_addrs[j] = addr;
            } else if (syms[j].st_shndx == SHN_ABS) {
                sym_addrs[j] = syms[j].st_value;
            } else {
                uint16_t sec = syms[j].st_shndx;
                if (sec < MAX_SECTIONS &&
                    s->section_offsets[sec] != SECTION_NOT_LOADED) {
                    sym_addrs[j] = s->base_addr + s->section_offsets[sec] +
                                   syms[j].st_value;
                }
            }
        }

        for (uint32_t j = 0; j < rel_count; j++) {
            uint32_t sym_idx = ELF32_R_SYM(rels[j].r_info);
            uint32_t type = ELF32_R_TYPE(rels[j].r_info);

            uint32_t mem_offset =
                s->section_offsets[target_sec] + rels[j].r_offset;
            uint8_t *target = s->loaded_mem + mem_offset;
            uint32_t target_addr = s->base_addr + mem_offset;
            uint32_t sym_addr = sym_addrs[sym_idx];

            switch (type) {
            case R_ARM_ABS32: {
                uint32_t val;
                memcpy(&val, target, 4);
                val += sym_addr;
                memcpy(target, &val, 4);
                break;
            }
            case R_ARM_THM_CALL:
            case R_ARM_THM_JUMP24: {
                uint16_t upper, lower;
                memcpy(&upper, target, 2);
                memcpy(&lower, target + 2, 2);

                int32_t off = (int32_t) sym_addr - (int32_t) (target_addr + 4);
                off >>= 1;

                uint32_t sign = (off >> 23) & 1;
                uint32_t j1 = ((off >> 22) & 1) ^ sign ^ 1;
                uint32_t j2 = ((off >> 21) & 1) ^ sign ^ 1;
                uint32_t imm10 = (off >> 11) & 0x3FF;
                uint32_t imm11 = off & 0x7FF;

                upper = (upper & 0xF800) | (sign << 10) | imm10;
                lower = (lower & 0xD000) | (j1 << 13) | (j2 << 11) | imm11;

                memcpy(target, &upper, 2);
                memcpy(target + 2, &lower, 2);
                break;
            }
            case R_ARM_THM_MOVW_ABS_NC:
            case R_ARM_THM_MOVT_ABS: {
                uint16_t val16 = (type == R_ARM_THM_MOVT_ABS)
                                     ? (sym_addr >> 16)
                                     : (sym_addr & 0xFFFF);

                uint16_t upper, lower;
                memcpy(&upper, target, 2);
                memcpy(&lower, target + 2, 2);

                uint32_t imm4 = (val16 >> 12) & 0xF;
                uint32_t ii = (val16 >> 11) & 0x1;
                uint32_t imm3 = (val16 >> 8) & 0x7;
                uint32_t imm8 = val16 & 0xFF;

                upper = (upper & 0xFBF0) | (ii << 10) | imm4;
                lower = (lower & 0x0F00) | (imm3 << 12) | imm8;

                memcpy(target, &upper, 2);
                memcpy(target + 2, &lower, 2);
                break;
            }
            default:
                vPortFree(sym_addrs);
                return -1;
            }
        }

        vPortFree(sym_addrs);
    }

    return 0;
}

static uint32_t loader_find_entry(LoaderState_t *s)
{
    for (uint16_t i = 0; i < s->ehdr->e_shnum; i++) {
        if (s->shdrs[i].sh_type != SHT_SYMTAB)
            continue;

        const Elf32_Sym *syms =
            (const Elf32_Sym *) (s->elf_data + s->shdrs[i].sh_offset);
        uint32_t sym_count = s->shdrs[i].sh_size / sizeof(Elf32_Sym);
        const Elf32_Shdr *strtab_shdr = &s->shdrs[s->shdrs[i].sh_link];
        const char *strtab =
            (const char *) (s->elf_data + strtab_shdr->sh_offset);

        for (uint32_t j = 0; j < sym_count; j++) {
            if (strcmp(strtab + syms[j].st_name, "app_main") == 0 &&
                syms[j].st_shndx != SHN_UNDEF) {
                uint16_t sec = syms[j].st_shndx;
                if (sec >= MAX_SECTIONS ||
                    s->section_offsets[sec] == SECTION_NOT_LOADED)
                    continue;
                return s->base_addr + s->section_offsets[sec] +
                       syms[j].st_value;
            }
        }
    }
    return 0;
}

ElfLoaderResult_t elf_loader_load(const uint8_t *elf_data, uint32_t elf_size)
{
    assert(elf_data);
    assert(elf_size > 0);

    ElfLoaderResult_t result = {0};
    LoaderState_t s = {0};
    s.elf_data = elf_data;
    s.elf_size = elf_size;

    if (loader_validate(&s) != 0)
        return result;

    if (loader_load_sections(&s) != 0)
        return result;

    if (loader_resolve_and_relocate(&s) != 0) {
        vPortFree(s.loaded_mem);
        return result;
    }

    __DSB();
    __ISB();

    uint32_t entry = loader_find_entry(&s);
    if (entry == 0) {
        vPortFree(s.loaded_mem);
        return result;
    }

    result.entry = (TaskFunction_t) (entry | 1);
    result.memory = s.loaded_mem;
    result.memory_size = s.loaded_size;
    return result;
}
