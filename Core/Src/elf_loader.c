#include <string.h>

#include "app_config.h"
#include "elf.h"
#include "elf_loader.h"

#define MAX_SECTIONS 32
#define SECTION_NOT_LOADED 0xFFFFFFFF
#define MAX_LOADED_SIZE APP_MAX_FILE_SIZE
#define ALIGN_UP(val, align) (((val) + (align) - 1) & ~((align) - 1))
#define IS_POWER_OF_TWO(x) ((x) != 0 && ((x) & ((x) - 1)) == 0)

typedef struct {
    const Elf32_Ehdr *ehdr;
    const Elf32_Shdr *shdrs;
    const uint8_t *elf_data;
    uint32_t elf_size;
    uint8_t *loaded_mem;
    uint32_t loaded_size;
    uint32_t base_addr;
    uint32_t section_offsets[MAX_SECTIONS];
    symbol_resolver_t resolver;
} loader_state_t;

static const char *status_strings[] = {
    [ELF_OK] = "ok",
    [ELF_ERR_NULL_ARG] = "null argument",
    [ELF_ERR_TOO_SMALL] = "file too small",
    [ELF_ERR_BAD_MAGIC] = "bad ELF magic",
    [ELF_ERR_BAD_CLASS] = "not ELF32",
    [ELF_ERR_BAD_ENDIAN] = "not little-endian",
    [ELF_ERR_BAD_TYPE] = "not ET_REL",
    [ELF_ERR_BAD_MACHINE] = "not ARM",
    [ELF_ERR_NO_MEMORY] = "out of memory",
    [ELF_ERR_UNRESOLVED_SYMBOL] = "unresolved symbol",
    [ELF_ERR_UNSUPPORTED_RELOC] = "unsupported relocation",
    [ELF_ERR_NO_ENTRY] = "app_main not found",
    [ELF_ERR_OUT_OF_BOUNDS] = "malformed ELF (out of bounds)",
    [ELF_ERR_RELOC_RANGE] = "relocation out of range",
};

const char *elf_status_str(elf_status_t status)
{
    if (status < sizeof(status_strings) / sizeof(status_strings[0]) &&
        status_strings[status])
        return status_strings[status];
    return "unknown error";
}

/* Return a NUL-terminated string at offset, or NULL if it would run past
 * the end of the string table. */
static const char *str_at(const char *strtab,
                          uint32_t strtab_size,
                          uint32_t offset)
{
    if (offset >= strtab_size)
        return NULL;
    if (!memchr(strtab + offset, '\0', strtab_size - offset))
        return NULL;
    return strtab + offset;
}

static elf_status_t loader_validate(loader_state_t *s)
{
    if (s->elf_size < sizeof(Elf32_Ehdr))
        return ELF_ERR_TOO_SMALL;

    const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *) s->elf_data;

    if (memcmp(ehdr->e_ident, ELF_MAGIC, ELF_MAGIC_SIZE) != 0)
        return ELF_ERR_BAD_MAGIC;
    if (ehdr->e_ident[4] != ELFCLASS32)
        return ELF_ERR_BAD_CLASS;
    if (ehdr->e_ident[5] != ELFDATA2LSB)
        return ELF_ERR_BAD_ENDIAN;
    if (ehdr->e_type != ET_REL)
        return ELF_ERR_BAD_TYPE;
    if (ehdr->e_machine != EM_ARM)
        return ELF_ERR_BAD_MACHINE;

    if (ehdr->e_shentsize != sizeof(Elf32_Shdr))
        return ELF_ERR_OUT_OF_BOUNDS;
    if (ehdr->e_shnum == 0 || ehdr->e_shnum > MAX_SECTIONS)
        return ELF_ERR_OUT_OF_BOUNDS;
    if (ehdr->e_shoff > s->elf_size ||
        ehdr->e_shnum * sizeof(Elf32_Shdr) > s->elf_size - ehdr->e_shoff)
        return ELF_ERR_OUT_OF_BOUNDS;

    s->ehdr = ehdr;
    s->shdrs = (const Elf32_Shdr *) (s->elf_data + ehdr->e_shoff);

    for (uint16_t i = 0; i < ehdr->e_shnum; i++) {
        if (s->shdrs[i].sh_type == SHT_NOBITS)
            continue;
        if (s->shdrs[i].sh_offset > s->elf_size ||
            s->shdrs[i].sh_size > s->elf_size - s->shdrs[i].sh_offset)
            return ELF_ERR_OUT_OF_BOUNDS;
    }

    return ELF_OK;
}

static elf_status_t loader_load_sections(loader_state_t *s)
{
    uint32_t total = 0;

    /* loader_validate() guarantees e_shnum <= MAX_SECTIONS */
    for (int i = 0; i < s->ehdr->e_shnum; i++) {
        s->section_offsets[i] = SECTION_NOT_LOADED;

        if (!(s->shdrs[i].sh_flags & SHF_ALLOC))
            continue;

        if (s->shdrs[i].sh_size > MAX_LOADED_SIZE)
            return ELF_ERR_OUT_OF_BOUNDS;

        uint32_t align = s->shdrs[i].sh_addralign;
        if (align > 1) {
            if (!IS_POWER_OF_TWO(align) || align > MAX_LOADED_SIZE)
                return ELF_ERR_OUT_OF_BOUNDS;
            total = ALIGN_UP(total, align);
        }

        s->section_offsets[i] = total;
        total += s->shdrs[i].sh_size;

        if (total > MAX_LOADED_SIZE)
            return ELF_ERR_OUT_OF_BOUNDS;
    }

    s->loaded_mem = pvPortMalloc(total);
    if (!s->loaded_mem)
        return ELF_ERR_NO_MEMORY;

    s->loaded_size = total;
    s->base_addr = (uint32_t) s->loaded_mem;
    memset(s->loaded_mem, 0, total);

    for (int i = 0; i < s->ehdr->e_shnum; i++) {
        if (s->section_offsets[i] == SECTION_NOT_LOADED)
            continue;

        uint32_t offset = s->section_offsets[i];

        if (s->shdrs[i].sh_type != SHT_NOBITS) {
            memcpy(s->loaded_mem + offset, s->elf_data + s->shdrs[i].sh_offset,
                   s->shdrs[i].sh_size);
        }
    }

    return ELF_OK;
}

/* Structurally validate the symbol table at section index symtab_idx and
 * return pointers to its symbols and linked string table.  Shared by the
 * relocation pass and loader_find_symbol(). */
static elf_status_t get_symtab(loader_state_t *s,
                               uint16_t symtab_idx,
                               const Elf32_Sym **syms_out,
                               uint32_t *sym_count_out,
                               const char **strtab_out,
                               uint32_t *strtab_size_out)
{
    if (symtab_idx >= s->ehdr->e_shnum)
        return ELF_ERR_OUT_OF_BOUNDS;

    const Elf32_Shdr *symtab_shdr = &s->shdrs[symtab_idx];
    if (symtab_shdr->sh_type != SHT_SYMTAB ||
        symtab_shdr->sh_link >= s->ehdr->e_shnum ||
        symtab_shdr->sh_size % sizeof(Elf32_Sym) != 0)
        return ELF_ERR_OUT_OF_BOUNDS;

    const Elf32_Shdr *strtab_shdr = &s->shdrs[symtab_shdr->sh_link];
    if (strtab_shdr->sh_type != SHT_STRTAB)
        return ELF_ERR_OUT_OF_BOUNDS;

    *syms_out = (const Elf32_Sym *) (s->elf_data + symtab_shdr->sh_offset);
    *sym_count_out = symtab_shdr->sh_size / sizeof(Elf32_Sym);
    *strtab_out = (const char *) (s->elf_data + strtab_shdr->sh_offset);
    *strtab_size_out = strtab_shdr->sh_size;
    return ELF_OK;
}

/* Relocation support: apps built with apps/Makefile (-mlong-calls)
 * only produce R_ARM_ABS32 in practice, which is the tested path.  The
 * Thumb branch/MOV handlers below are validated but best-effort;
 * arbitrary .o files from other toolchains/flags are not a supported
 * input and may be rejected. */

/* Thumb BL/B.W: patch a signed 24-bit halfword offset into the
 * instruction pair.  Branches outside the encodable range (about
 * +/-16 MB) would be silently truncated, so reject them instead. */
static elf_status_t apply_thm_branch(uint8_t *target,
                                     uint32_t target_addr,
                                     uint32_t sym_addr)
{
    uint16_t upper, lower;
    memcpy(&upper, target, 2);
    memcpy(&lower, target + 2, 2);

    int32_t off = (int32_t) sym_addr - (int32_t) (target_addr + 4);
    off >>= 1;
    if (off < -(1 << 23) || off >= (1 << 23))
        return ELF_ERR_RELOC_RANGE;

    uint32_t sign = (off >> 23) & 1;
    uint32_t j1 = ((off >> 22) & 1) ^ sign ^ 1;
    uint32_t j2 = ((off >> 21) & 1) ^ sign ^ 1;
    uint32_t imm10 = (off >> 11) & 0x3FF;
    uint32_t imm11 = off & 0x7FF;

    upper = (upper & 0xF800) | (sign << 10) | imm10;
    lower = (lower & 0xD000) | (j1 << 13) | (j2 << 11) | imm11;

    memcpy(target, &upper, 2);
    memcpy(target + 2, &lower, 2);
    return ELF_OK;
}

/* Thumb MOVW/MOVT: for REL relocations the addend is the immediate
 * already encoded in the instruction, interpreted as a signed 16-bit
 * value (AAELF), so decode it and patch sym_addr + addend back in. */
static void apply_thm_mov(uint8_t *target, uint32_t sym_addr, int is_movt)
{
    uint16_t upper, lower;
    memcpy(&upper, target, 2);
    memcpy(&lower, target + 2, 2);

    uint32_t imm4 = upper & 0xF;
    uint32_t ii = (upper >> 10) & 0x1;
    uint32_t imm3 = (lower >> 12) & 0x7;
    uint32_t imm8 = lower & 0xFF;
    int32_t addend =
        (int32_t) (int16_t) ((imm4 << 12) | (ii << 11) | (imm3 << 8) | imm8);

    uint32_t value = sym_addr + (uint32_t) addend;
    uint16_t val16 = is_movt ? (value >> 16) : (value & 0xFFFF);

    imm4 = (val16 >> 12) & 0xF;
    ii = (val16 >> 11) & 0x1;
    imm3 = (val16 >> 8) & 0x7;
    imm8 = val16 & 0xFF;

    upper = (upper & 0xFBF0) | (ii << 10) | imm4;
    lower = (lower & 0x0F00) | (imm3 << 12) | imm8;

    memcpy(target, &upper, 2);
    memcpy(target + 2, &lower, 2);
}

static elf_status_t loader_resolve_and_relocate(loader_state_t *s)
{
    for (int i = 0; i < s->ehdr->e_shnum; i++) {
        if (s->shdrs[i].sh_type != SHT_REL)
            continue;

        uint32_t target_sec = s->shdrs[i].sh_info;
        if (target_sec >= s->ehdr->e_shnum)
            return ELF_ERR_OUT_OF_BOUNDS;
        if (s->section_offsets[target_sec] == SECTION_NOT_LOADED)
            continue;

        if (s->shdrs[i].sh_size % sizeof(Elf32_Rel) != 0)
            return ELF_ERR_OUT_OF_BOUNDS;

        const Elf32_Rel *rels =
            (const Elf32_Rel *) (s->elf_data + s->shdrs[i].sh_offset);
        uint32_t rel_count = s->shdrs[i].sh_size / sizeof(Elf32_Rel);

        const Elf32_Sym *syms;
        uint32_t sym_count;
        const char *strtab;
        uint32_t strtab_size;
        elf_status_t rc = get_symtab(s, s->shdrs[i].sh_link, &syms, &sym_count,
                                     &strtab, &strtab_size);
        if (rc != ELF_OK)
            return rc;

        /* sym_addrs[j] followed by a resolved flag per symbol, so a
         * relocation against an unresolvable symbol is rejected instead
         * of silently using address 0. */
        uint32_t *sym_addrs = pvPortMalloc(sym_count * (sizeof(uint32_t) + 1));
        if (!sym_addrs)
            return ELF_ERR_NO_MEMORY;
        uint8_t *resolved = (uint8_t *) (sym_addrs + sym_count);
        memset(sym_addrs, 0, sym_count * (sizeof(uint32_t) + 1));

        for (uint32_t j = 1; j < sym_count; j++) {
            if (syms[j].st_shndx == SHN_UNDEF) {
                const char *name = str_at(strtab, strtab_size, syms[j].st_name);
                if (!name) {
                    vPortFree(sym_addrs);
                    return ELF_ERR_OUT_OF_BOUNDS;
                }
                uint32_t addr = s->resolver(name);
                if (addr == 0) {
                    vPortFree(sym_addrs);
                    return ELF_ERR_UNRESOLVED_SYMBOL;
                }
                sym_addrs[j] = addr;
                resolved[j] = 1;
            } else if (syms[j].st_shndx == SHN_ABS) {
                sym_addrs[j] = syms[j].st_value;
                resolved[j] = 1;
            } else {
                uint16_t sec = syms[j].st_shndx;
                if (sec < s->ehdr->e_shnum &&
                    s->section_offsets[sec] != SECTION_NOT_LOADED) {
                    if (syms[j].st_value > s->shdrs[sec].sh_size) {
                        vPortFree(sym_addrs);
                        return ELF_ERR_OUT_OF_BOUNDS;
                    }
                    sym_addrs[j] = s->base_addr + s->section_offsets[sec] +
                                   syms[j].st_value;
                    resolved[j] = 1;
                }
            }
        }

        for (uint32_t j = 0; j < rel_count; j++) {
            uint32_t sym_idx = ELF32_R_SYM(rels[j].r_info);
            uint32_t type = ELF32_R_TYPE(rels[j].r_info);

            /* Subtract instead of adding 4 to r_offset: the addition
             * could wrap and slip past the bounds check. */
            if (sym_idx >= sym_count ||
                rels[j].r_offset > s->shdrs[target_sec].sh_size ||
                s->shdrs[target_sec].sh_size - rels[j].r_offset < 4) {
                vPortFree(sym_addrs);
                return ELF_ERR_OUT_OF_BOUNDS;
            }
            if (!resolved[sym_idx]) {
                vPortFree(sym_addrs);
                return ELF_ERR_UNRESOLVED_SYMBOL;
            }

            /* R_ARM_ABS32 patches a word, Thumb relocations patch
             * halfword pairs */
            uint32_t align_mask = (type == R_ARM_ABS32) ? 3 : 1;
            if (rels[j].r_offset & align_mask) {
                vPortFree(sym_addrs);
                return ELF_ERR_OUT_OF_BOUNDS;
            }

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
                elf_status_t brc =
                    apply_thm_branch(target, target_addr, sym_addr);
                if (brc != ELF_OK) {
                    vPortFree(sym_addrs);
                    return brc;
                }
                break;
            }
            case R_ARM_THM_MOVW_ABS_NC:
            case R_ARM_THM_MOVT_ABS:
                apply_thm_mov(target, sym_addr, type == R_ARM_THM_MOVT_ABS);
                break;
            default:
                vPortFree(sym_addrs);
                return ELF_ERR_UNSUPPORTED_RELOC;
            }
        }

        vPortFree(sym_addrs);
    }

    return ELF_OK;
}

static uint32_t loader_find_symbol(loader_state_t *s, const char *name)
{
    for (uint16_t i = 0; i < s->ehdr->e_shnum; i++) {
        if (s->shdrs[i].sh_type != SHT_SYMTAB)
            continue;

        const Elf32_Sym *syms;
        uint32_t sym_count;
        const char *strtab;
        uint32_t strtab_size;
        if (get_symtab(s, i, &syms, &sym_count, &strtab, &strtab_size) !=
            ELF_OK)
            continue;

        for (uint32_t j = 0; j < sym_count; j++) {
            const char *sym_name = str_at(strtab, strtab_size, syms[j].st_name);
            if (!sym_name)
                continue;
            if (strcmp(sym_name, name) == 0 && syms[j].st_shndx != SHN_UNDEF) {
                uint16_t sec = syms[j].st_shndx;
                if (sec >= s->ehdr->e_shnum ||
                    s->section_offsets[sec] == SECTION_NOT_LOADED ||
                    syms[j].st_value >= s->shdrs[sec].sh_size)
                    continue;
                return s->base_addr + s->section_offsets[sec] +
                       syms[j].st_value;
            }
        }
    }
    return 0;
}

elf_status_t elf_load(const uint8_t *elf_data,
                      uint32_t elf_size,
                      symbol_resolver_t resolver,
                      elf_load_result_t *out)
{
    if (!elf_data || !elf_size || !resolver || !out)
        return ELF_ERR_NULL_ARG;

    memset(out, 0, sizeof(*out));
    loader_state_t s = {0};
    s.elf_data = elf_data;
    s.elf_size = elf_size;
    s.resolver = resolver;

    elf_status_t rc = loader_validate(&s);
    if (rc != ELF_OK)
        return rc;

    rc = loader_load_sections(&s);
    if (rc != ELF_OK)
        return rc;

    rc = loader_resolve_and_relocate(&s);
    if (rc != ELF_OK) {
        vPortFree(s.loaded_mem);
        return rc;
    }

    __asm volatile("dsb" ::: "memory");
    __asm volatile("isb" ::: "memory");

    uint32_t entry = loader_find_symbol(&s, "app_main");
    if (entry == 0) {
        vPortFree(s.loaded_mem);
        return ELF_ERR_NO_ENTRY;
    }

    out->entry = (TaskFunction_t) (entry | 1);
    out->memory = s.loaded_mem;
    out->memory_size = s.loaded_size;

    uint32_t cleanup = loader_find_symbol(&s, "app_cleanup");
    if (cleanup != 0)
        out->cleanup = (void (*)(void))(cleanup | 1);

    return ELF_OK;
}
