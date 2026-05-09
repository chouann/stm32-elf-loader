#include "elf_rw.h"
#include "myprintf.h"
#include <string.h>

FIL ELFFile;
static ELF_BufferTypeDef BufferCtl;
static Elf32_Ehdr ELFHeader;

/**
  * @brief  Opens an ELF file from storage.
  * @param  filename: Name of the ELF file
  * @retval ELF error code
  */
ELF_ErrorTypeDef ELF_Open(char *filename)
{
    FRESULT res;
    
    /* Close any previously opened file */
    f_close(&ELFFile);
    
    /* Open the ELF file */
    res = f_open(&ELFFile, filename, FA_READ);
    if(res != FR_OK)
    {
        return ELF_ERROR_IO;
    }
    
    /* Seek to beginning */
    f_lseek(&ELFFile, 0);
    BufferCtl.fptr = 0;
    BufferCtl.state = BUFFER_OFFSET_NONE;
    
    return ELF_ERROR_NONE;
}

/**
  * @brief  Reads the ELF header from the file.
  * @param  header: Pointer to store ELF header
  * @retval ELF error code
  */
ELF_ErrorTypeDef ELF_ReadHeader(Elf32_Ehdr *header)
{
    uint32_t bytesread;
    FRESULT res;
    
    if(header == NULL)
    {
        return ELF_ERROR_IO;
    }
    
    /* Seek to start of file */
    f_lseek(&ELFFile, 0);
    
    /* Read ELF header (52 bytes) */
    res = f_read(&ELFFile, header, sizeof(Elf32_Ehdr), (void *)&bytesread);
    if(res != FR_OK || bytesread != sizeof(Elf32_Ehdr))
    {
        return ELF_ERROR_IO;
    }
    
    /* Validate ELF magic number */
    if(header->e_ident[0] != 0x7f || header->e_ident[1] != 'E' || 
       header->e_ident[2] != 'L' || header->e_ident[3] != 'F')
    {
        return ELF_ERROR_INVALID_ELF;
    }
    
    /* Validate it's an ARM relocatable object file */
    if(header->e_machine != EM_ARM || header->e_type != ET_REL)
    {
        return ELF_ERROR_UNSUPPORTED;
    }
    
    /* Store header for later use */
    ELFHeader = *header;
    
    return ELF_ERROR_NONE;
}

/**
  * @brief  Reads a section header from the ELF file.
  * @param  index: Section index
  * @param  section: Pointer to store section header
  * @retval ELF error code
  */
ELF_ErrorTypeDef ELF_ReadSectionHeader(uint32_t index, Elf32_Shdr *section)
{
    uint32_t bytesread;
    uint32_t offset;
    FRESULT res;
    
    if(section == NULL || index >= ELFHeader.e_shnum)
    {
        return ELF_ERROR_IO;
    }
    
    /* Calculate section header offset */
    offset = ELFHeader.e_shoff + (index * ELFHeader.e_shentsize);
    
    /* Seek to section header */
    f_lseek(&ELFFile, offset);
    
    /* Read section header */
    res = f_read(&ELFFile, section, sizeof(Elf32_Shdr), (void *)&bytesread);
    if(res != FR_OK || bytesread != sizeof(Elf32_Shdr))
    {
        return ELF_ERROR_IO;
    }
    
    return ELF_ERROR_NONE;
}

/**
  * @brief  Prints hexdump of ELF file raw bytes
  * @param  filename: Name of the ELF file
  * @param  max_bytes: Maximum bytes to dump (0 = all)
  * @retval ELF error code
  */
ELF_ErrorTypeDef ELF_hexdump(char *filename, uint32_t max_bytes)
{
    FIL file;
    FILINFO fno;
    FRESULT res;
    uint32_t bytesread, i, offset = 0;
    uint8_t buffer[16];
    
    myprintf("\r\n=== Hexdump: %s ===\r\n", filename);
    
    /* Get file size */
    res = f_stat(filename, &fno);
    if(res != FR_OK)
        return ELF_ERROR_IO;
    
    uint32_t file_size = fno.fsize;
    uint32_t bytes_to_read = (max_bytes == 0 || max_bytes > file_size) ? file_size : max_bytes;
    
    myprintf("File size: %u bytes, dumping %u bytes\r\n\r\n", file_size, bytes_to_read);
    
    /* Open and read file */
    res = f_open(&file, filename, FA_READ);
    if(res != FR_OK)
        return ELF_ERROR_IO;
    
    /* Hexdump loop */
    while(offset < bytes_to_read)
    {
        uint32_t chunk_size = (bytes_to_read - offset) < 16 ? (bytes_to_read - offset) : 16;
        res = f_read(&file, buffer, chunk_size, (void *)&bytesread);
        if(res != FR_OK || bytesread == 0)
            break;
        
        /* Print offset and hex bytes */
        myprintf("%08x  ", offset);
        for(i = 0; i < 16; i++)
        {
            if(i < bytesread)
                myprintf("%02x ", buffer[i]);
            else
                myprintf("   ");
            
            /* Add extra space between 8-byte groups */
            if(i == 7)
                myprintf(" ");
        }
        
        /* Print ASCII representation */
        myprintf(" |");
        for(i = 0; i < bytesread; i++)
        {
            char c = buffer[i];
            myprintf("%c", (c >= 32 && c < 127) ? c : '.');
        }
        myprintf("|\r\n");
        
        offset += bytesread;
    }
    
    f_close(&file);
    myprintf("=== End Hexdump ===\r\n\r\n");
    
    return ELF_ERROR_NONE;
}

ELF_ErrorTypeDef ELF_inspect(char *filename)
{
    Elf32_Ehdr header;
    Elf32_Shdr section, symtab_shdr, strtab_shdr, shstrtab_shdr;
    Elf32_Sym *symbols;
    Elf32_Rel *relocations;
    uint8_t *section_data;
    uint32_t i, j, sym_count, rel_count;
    const char *shstrtab, *strtab;
    
    myprintf("\r\n=== ELF File Inspection: %s ===\r\n\r\n", filename);
    
    /* Open and read ELF header */
    if(ELF_Open(filename) != ELF_ERROR_NONE)
    {
        myprintf("ERROR: Cannot open file\r\n");
        return ELF_ERROR_IO;
    }
    
    if(ELF_ReadHeader(&header) != ELF_ERROR_NONE)
    {
        myprintf("ERROR: Invalid ELF file\r\n");
        return ELF_ERROR_INVALID_ELF;
    }
    
    /* Print ELF Header */
    myprintf("################### %s ###################\r\n", filename);
    myprintf("ELF Header:\r\n");
    myprintf("  Magic:   %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\r\n",
             header.e_ident[0], header.e_ident[1], header.e_ident[2], header.e_ident[3],
             header.e_ident[4], header.e_ident[5], header.e_ident[6], header.e_ident[7],
             header.e_ident[8], header.e_ident[9], header.e_ident[10], header.e_ident[11],
             header.e_ident[12], header.e_ident[13], header.e_ident[14], header.e_ident[15]);
    myprintf("  Class:                             ELF%d\r\n", header.e_ident[4] == ELFCLASS32 ? 32 : 64);
    myprintf("  Data:                              2's complement, %s\r\n", 
             header.e_ident[5] == ELFDATA2LSB ? "little endian" : "big endian");
    myprintf("  Version:                           1 (current)\r\n");
    myprintf("  OS/ABI:                            UNIX - System V\r\n");
    myprintf("  ABI Version:                       0\r\n");
    myprintf("  Type:                              %s\r\n", 
             header.e_type == ET_REL ? "REL (Relocatable file)" : "EXEC (Executable file)");
    myprintf("  Machine:                           ARM\r\n");
    myprintf("  Version:                           0x%x\r\n", header.e_version);
    myprintf("  Entry point address:               0x%x\r\n", header.e_entry);
    myprintf("  Start of program headers:          %u (bytes into file)\r\n", header.e_phoff);
    myprintf("  Start of section headers:          %u (bytes into file)\r\n", header.e_shoff);
    myprintf("  Flags:                             0x%x, Version5 EABI\r\n", header.e_flags);
    myprintf("  Size of this header:               %u (bytes)\r\n", header.e_ehsize);
    myprintf("  Size of program headers:           %u (bytes)\r\n", header.e_phentsize);
    myprintf("  Number of program headers:         %u\r\n", header.e_phnum);
    myprintf("  Size of section headers:           %u (bytes)\r\n", header.e_shentsize);
    myprintf("  Number of section headers:         %u\r\n", header.e_shnum);
    myprintf("  Section header string table index: %u\r\n", header.e_shstrndx);
    
    /* Read section header string table */
    if(ELF_ReadSectionHeader(header.e_shstrndx, &shstrtab_shdr) != ELF_ERROR_NONE)
    {
        myprintf("ERROR: Cannot read shstrtab\r\n");
        return ELF_ERROR_IO;
    }
    
    section_data = pvPortMalloc(shstrtab_shdr.sh_size);
    if(section_data == NULL)
        return ELF_ERROR_IO;
    
    if(ELF_ReadSectionData(&shstrtab_shdr, section_data, shstrtab_shdr.sh_size) != ELF_ERROR_NONE)
    {
        vPortFree(section_data);
        return ELF_ERROR_IO;
    }
    
    shstrtab = (const char *)section_data;
    
    /* Print Section Headers */
    myprintf("\r\nSection Headers:\r\n");
    myprintf("  [Nr] Name              Type            Addr     Off    Size   ES Flg Lk Inf Al\r\n");
    
    for(i = 0; i < header.e_shnum; i++)
    {
        if(ELF_ReadSectionHeader(i, &section) != ELF_ERROR_NONE)
            continue;
        
        const char *name = section.sh_name < shstrtab_shdr.sh_size ? 
                          &shstrtab[section.sh_name] : "";
        const char *type_str = "";
        
        switch(section.sh_type)
        {
            case SHT_NULL:          type_str = "NULL"; break;
            case SHT_PROGBITS:      type_str = "PROGBITS"; break;
            case SHT_SYMTAB:        type_str = "SYMTAB"; break;
            case SHT_STRTAB:        type_str = "STRTAB"; break;
            case SHT_REL:           type_str = "REL"; break;
            case SHT_NOBITS:        type_str = "NOBITS"; break;
            case SHT_ARM_ATTRIBUTES: type_str = "ARM_ATTRIBUTES"; break;
            default:                type_str = "OTHER"; break;
        }
        
        char flags[16] = "";
        if(section.sh_flags & SHF_WRITE)     strncat(flags, "W", sizeof(flags)-1);
        if(section.sh_flags & SHF_ALLOC)     strncat(flags, "A", sizeof(flags)-1);
        if(section.sh_flags & SHF_EXECINSTR) strncat(flags, "X", sizeof(flags)-1);
        if(section.sh_flags & SHF_MERGE)     strncat(flags, "M", sizeof(flags)-1);
        if(section.sh_flags & SHF_STRINGS)   strncat(flags, "S", sizeof(flags)-1);
        if(section.sh_flags & SHF_INFO_LINK) strncat(flags, "I", sizeof(flags)-1);
        
        myprintf("  [%2u] %-16s %-15s %08x %06x %06x %2u %3s %2u %2u %2u\r\n",
                 i, name, type_str, section.sh_addr, section.sh_offset, 
                 section.sh_size, section.sh_entsize, flags, section.sh_link, 
                 section.sh_info, section.sh_addralign);
    }
    
    myprintf("Key to Flags:\r\n");
    myprintf("  W (write), A (alloc), X (execute), M (merge), S (strings), I (info),\r\n");
    myprintf("  L (link order), O (extra OS processing required), G (group), T (TLS),\r\n");
    myprintf("  C (compressed), x (unknown), o (OS specific), E (exclude),\r\n");
    myprintf("  D (mbind), y (purecode), p (processor specific)\r\n");
    
    /* Find and print symbol table */
    for(i = 0; i < header.e_shnum; i++)
    {
        if(ELF_ReadSectionHeader(i, &section) != ELF_ERROR_NONE)
            continue;
        
        if(section.sh_type != SHT_SYMTAB)
            continue;
        
        /* Read symbol table */
        symtab_shdr = section;
        sym_count = section.sh_size / sizeof(Elf32_Sym);
        
        symbols = pvPortMalloc(section.sh_size);
        if(symbols == NULL)
            break;
        ELF_ReadSectionData(&section, (uint8_t *)symbols, section.sh_size);
        
        /* Read string table */
        ELF_ReadSectionHeader(section.sh_link, &strtab_shdr);
        strtab = pvPortMalloc(strtab_shdr.sh_size);
        if(strtab == NULL)
        {
            vPortFree(symbols);
            break;
        }
        ELF_ReadSectionData(&strtab_shdr, (uint8_t *)strtab, strtab_shdr.sh_size);
        
        /* Print symbol table */
        myprintf("\r\nSymbol table '.symtab' contains %u entries:\r\n", sym_count);
        myprintf("   Num:    Value  Size Type    Bind   Vis      Ndx Name\r\n");
        
        for(j = 0; j < sym_count; j++)
        {
            Elf32_Sym *sym = &symbols[j];
            const char *bind_str = "";
            const char *type_str = "";
            const char *ndx_str = "";
            const char *sym_name = "";
            
            /* Get bind */
            uint32_t bind = ELF32_ST_BIND(sym->st_info);
            if(bind == 0)
                bind_str = "LOCAL";
            else if(bind == 1)
                bind_str = "GLOBAL";
            else if(bind == 2)
                bind_str = "WEAK";
            else
                bind_str = "OTHER";
            
            /* Get type */
            uint32_t sym_type = ELF32_ST_TYPE(sym->st_info);
            if(sym_type == 0)
                type_str = "NOTYPE";
            else if(sym_type == 1)
                type_str = "OBJECT";
            else if(sym_type == 2)
                type_str = "FUNC";
            else if(sym_type == 3)
                type_str = "SECTION";
            else if(sym_type == 4)
                type_str = "FILE";
            else
                type_str = "OTHER";
            
            /* Get section index string */
            if(sym->st_shndx == SHN_UNDEF)
                ndx_str = "UND";
            else if(sym->st_shndx == SHN_ABS)
                ndx_str = "ABS";
            else if(sym->st_shndx == SHN_COMMON)
                ndx_str = "COM";
            
            /* Get symbol name */
            if(sym->st_name > 0 && sym->st_name < strtab_shdr.sh_size)
                sym_name = &strtab[sym->st_name];
            else if(sym->st_shndx != SHN_UNDEF && sym->st_shndx < header.e_shnum)
            {
                /* For section symbols without name, try to get section name */
                if(ELF32_ST_TYPE(sym->st_info) == STT_SECTION)
                {
                    Elf32_Shdr sec;
                    if(ELF_ReadSectionHeader(sym->st_shndx, &sec) == ELF_ERROR_NONE)
                        sym_name = sec.sh_name < shstrtab_shdr.sh_size ? 
                                  &shstrtab[sec.sh_name] : "";
                }
            }
            
            myprintf("     %2u: %08x %5u %-7s %-5s DEFAULT ", j, sym->st_value, sym->st_size, type_str, bind_str);
            
            if(ndx_str[0] != '\0')
                myprintf("%3s", ndx_str);
            else if(sym->st_shndx != 0)
                myprintf("%3u", sym->st_shndx);
            else
                myprintf("  0");
            
            myprintf(" %s\r\n", sym_name);
        }
        
        vPortFree(symbols);
        vPortFree((void *)strtab);
    }
    
    /* Find and print relocations */
    for(i = 0; i < header.e_shnum; i++)
    {
        if(ELF_ReadSectionHeader(i, &section) != ELF_ERROR_NONE)
            continue;
        
        if(section.sh_type != SHT_REL)
            continue;
        
        const char *rel_name = section.sh_name < shstrtab_shdr.sh_size ? 
                              &shstrtab[section.sh_name] : "";
        
        rel_count = section.sh_size / sizeof(Elf32_Rel);
        relocations = pvPortMalloc(section.sh_size);
        if(relocations == NULL)
            break;
        ELF_ReadSectionData(&section, (uint8_t *)relocations, section.sh_size);
        
        myprintf("\r\nRelocation section '%s' at offset 0x%x contains %u entries:\r\n", 
                 rel_name, section.sh_offset, rel_count);
        myprintf(" Offset     Info    Type            Sym.Value  Sym. Name\r\n");
        
        /* Get symbol and string table for relocations */
        ELF_ReadSectionHeader(section.sh_link, &symtab_shdr);
        symbols = pvPortMalloc(symtab_shdr.sh_size);
        if(symbols)
        {
            ELF_ReadSectionData(&symtab_shdr, (uint8_t *)symbols, symtab_shdr.sh_size);
            
            ELF_ReadSectionHeader(symtab_shdr.sh_link, &strtab_shdr);
            strtab = pvPortMalloc(strtab_shdr.sh_size);
            if(strtab)
            {
                ELF_ReadSectionData(&strtab_shdr, (uint8_t *)strtab, strtab_shdr.sh_size);
                
                for(j = 0; j < rel_count; j++)
                {
                    uint32_t sym_idx = ELF32_R_SYM(relocations[j].r_info);
                    uint32_t rel_type = ELF32_R_TYPE(relocations[j].r_info);
                    
                    const char *sym_name = "";
                    uint32_t sym_value = 0;
                    
                    if(sym_idx < symtab_shdr.sh_size / sizeof(Elf32_Sym))
                    {
                        Elf32_Sym *sym = &symbols[sym_idx];
                        sym_value = sym->st_value;
                        if(sym->st_name < strtab_shdr.sh_size && sym->st_name > 0)
                            sym_name = &strtab[sym->st_name];
                        else if(ELF32_ST_TYPE(sym->st_info) == STT_SECTION && sym->st_shndx < header.e_shnum)
                        {
                            /* For section symbols, get section name */
                            Elf32_Shdr sec;
                            if(ELF_ReadSectionHeader(sym->st_shndx, &sec) == ELF_ERROR_NONE)
                                sym_name = sec.sh_name < shstrtab_shdr.sh_size ? 
                                          &shstrtab[sec.sh_name] : "";
                        }
                    }
                    
                    myprintf("%08x  %08x ", relocations[j].r_offset, relocations[j].r_info);
                    
                    if(rel_type == 2)
                        myprintf("R_ARM_ABS32     ");
                    else if(rel_type == 28)
                        myprintf("R_ARM_CALL      ");
                    else if(rel_type == 29)
                        myprintf("R_ARM_JUMP24    ");
                    else if(rel_type == 10)
                        myprintf("R_ARM_THM_CALL  ");
                    else if(rel_type == 30)
                        myprintf("R_ARM_THM_JUMP24");
                    else
                        myprintf("R_ARM_UNKNOWN   ");
                    
                    myprintf("%08x   %s\r\n", sym_value, sym_name);
                }
                
                vPortFree((void *)strtab);
            }
            vPortFree(symbols);
        }
        
        vPortFree(relocations);
    }
    
    vPortFree(section_data);
    ELF_Close();
    myprintf("\r\n");
    
    return ELF_ERROR_NONE;
}
/**
  * @brief  Reads section data from the ELF file.
  * @param  section: Section header
  * @param  buffer: Buffer to store section data
  * @param  size: Maximum size to read
  * @retval ELF error code
  */
ELF_ErrorTypeDef ELF_ReadSectionData(Elf32_Shdr *section, uint8_t *buffer, uint32_t size)
{
    uint32_t bytesread;
    uint32_t read_size;
    FRESULT res;
    
    if(section == NULL || buffer == NULL)
    {
        return ELF_ERROR_IO;
    }
    
    /* Don't read more than available or buffer size */
    read_size = (section->sh_size < size) ? section->sh_size : size;
    
    /* Seek to section data */
    f_lseek(&ELFFile, section->sh_offset);
    
    /* Read section data */
    res = f_read(&ELFFile, buffer, read_size, (void *)&bytesread);
    if(res != FR_OK)
    {
        return ELF_ERROR_IO;
    }
    
    return ELF_ERROR_NONE;
}

/**
  * @brief  Closes the ELF file.
  * @param  None
  * @retval ELF error code
  */
ELF_ErrorTypeDef ELF_Close(void)
{
    f_close(&ELFFile);
    BufferCtl.fptr = 0;
    BufferCtl.state = BUFFER_OFFSET_NONE;
    
    return ELF_ERROR_NONE;
}

/**
  * @brief  Reads the complete ELF file into a buffer.
  *         Allocates memory and reads entire file from SD card.
  *         Use with elf_loader_load(). Caller must free the buffer.
  * @param  filename: Name of the ELF file
  * @param  elf_data: Pointer to store allocated buffer address
  * @param  elf_size: Pointer to store file size
  * @retval ELF error code
  */
ELF_ErrorTypeDef ELF_ReadComplete(char *filename, uint8_t **elf_data, uint32_t *elf_size)
{
    FRESULT res;
    FILINFO fno;
    FIL file;
    uint32_t bytesread;
    uint8_t *buffer;
    
    if(filename == NULL || elf_data == NULL || elf_size == NULL)
    {
        return ELF_ERROR_IO;
    }
    
    /* Get file size */
    res = f_stat(filename, &fno);
    if(res != FR_OK)
    {
        return ELF_ERROR_IO;
    }
    
    uint32_t file_size = fno.fsize;
    if(file_size < sizeof(Elf32_Ehdr))
    {
        return ELF_ERROR_INVALID_ELF;
    }        
    myprintf("[ELF_ReadComplete]: try to read %s. File size = %d\r\n", filename, file_size); 
    /* Allocate memory for complete file */
    buffer = pvPortMalloc(file_size);
    if(buffer == NULL)
    {
        return ELF_ERROR_IO;
    }
    
    /* Open and read complete file */
    res = f_open(&file, filename, FA_READ);
    if(res != FR_OK)
    {
        vPortFree(buffer);
        return ELF_ERROR_IO;
    }
    
    res = f_read(&file, buffer, file_size, (void *)&bytesread);
    f_close(&file);
    
    if(res != FR_OK || bytesread != file_size)
    {
        vPortFree(buffer);
        return ELF_ERROR_IO;
    }
    
    /* Validate ELF header */
    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)buffer;
    if(ehdr->e_ident[0] != 0x7f || ehdr->e_ident[1] != 'E' || 
       ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F')
    {
        vPortFree(buffer);
        return ELF_ERROR_INVALID_ELF;
    }
    
    /* Validate ARM relocatable object */
    if(ehdr->e_machine != EM_ARM || ehdr->e_type != ET_REL)
    {
        vPortFree(buffer);
        return ELF_ERROR_UNSUPPORTED;
    }
    
    /* Return buffer and size */
    *elf_data = buffer;
    *elf_size = file_size;        
    
    myprintf("[ELF_ReadComplete]: Finish getting the elf data stored in elf_data.\r\n"); 
    
    return ELF_ERROR_NONE;
}

/**
  * @brief  Writes a buffer directly to a file.
  *         Creates or overwrites the file with the provided buffer contents.
  * @param  filename: Name of the file to write to
  * @param  buffer: Pointer to buffer containing data to write
  * @param  size: Number of bytes to write
  * @retval ELF error code
  */
ELF_ErrorTypeDef ELF_write(char *filename, uint8_t *buffer, uint32_t size)
{
    FRESULT res;
    FIL file;
    uint32_t byteswritten;
    
    if(filename == NULL || buffer == NULL || size == 0)
    {
        return ELF_ERROR_IO;
    }
    
    myprintf("[ELF_write]: Writing %u bytes to %s\r\n", size, filename);
    
    /* Open file for writing (create or overwrite) */
    res = f_open(&file, filename, FA_CREATE_ALWAYS | FA_WRITE);
    if(res != FR_OK)
    {
        myprintf("[ELF_write]: Failed to open file for writing\r\n");
        return ELF_ERROR_IO;
    }
    
    /* Write buffer to file */
    res = f_write(&file, buffer, size, (void *)&byteswritten);
    f_close(&file);
    
    if(res != FR_OK || byteswritten != size)
    {
        myprintf("[ELF_write]: Failed to write data. Written: %u/%u bytes\r\n", byteswritten, size);
        return ELF_ERROR_IO;
    }
    
    myprintf("[ELF_write]: Successfully wrote %u bytes to %s\r\n", byteswritten, filename);
    
    return ELF_ERROR_NONE;
}