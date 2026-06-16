#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stddef.h>

/* ELF64 заголовок — достаточно для разбора */
#define EI_NIDENT 16

typedef struct {
    uint8_t  e_ident[EI_NIDENT]; /* Magic + class + data + version + OS/ABI */
    uint16_t e_type;              /* ET_EXEC=2, ET_DYN=3, ET_REL=1 */
    uint16_t e_machine;           /* EM_X86_64=62, EM_AARCH64=183 */
    uint32_t e_version;
    uint64_t e_entry;             /* Точка входа */
    uint64_t e_phoff;             /* Смещение program headers */
    uint64_t e_shoff;             /* Смещение section headers */
    uint32_t e_flags;
    uint16_t e_ehsize;            /* Размер ELF заголовка */
    uint16_t e_phentsize;
    uint16_t e_phnum;             /* Количество program headers */
    uint16_t e_shentsize;
    uint16_t e_shnum;             /* Количество section headers */
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    int      valid;       /* 1 если ELF64 LE, 0 иначе */
    uint16_t e_type;      /* тип файла */
    uint16_t e_machine;   /* архитектура */
    uint64_t e_entry;     /* точка входа */
    uint16_t e_phnum;     /* число program headers */
    uint16_t e_shnum;     /* число section headers */
} elf_info_t;

/* Прочитать ELF заголовок из файла path.
   Заполнить info.
   Вернуть 0 если файл является ELF64 LE (little-endian).
   Вернуть -1 если файл не существует, слишком мал, или не ELF64 LE.
   Проверки:
   - e_ident[0..3] == "\x7fELF"
   - e_ident[4] == 2  (ELFCLASS64)
   - e_ident[5] == 1  (ELFDATA2LSB, little-endian)
*/
int elf_parse(const char *path, elf_info_t *info)
{
    (void)path; (void)info;
    return -1; /* TODO */
}
