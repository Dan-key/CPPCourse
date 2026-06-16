#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

/* ---- объявления из starter.c ---- */
#define EI_NIDENT 16

typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    int      valid;
    uint16_t e_type;
    uint16_t e_machine;
    uint64_t e_entry;
    uint16_t e_phnum;
    uint16_t e_shnum;
} elf_info_t;

int elf_parse(const char *path, elf_info_t *info);

/* ---- CHECK macro ---- */
static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { g_run++; if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } else { printf("  [FAIL] %s\n", msg); } } while(0)

/* ---- вспомогательные функции ---- */

/* Заполнить минимальный корректный ELF64 LE заголовок */
static void make_elf64_le(Elf64_Ehdr *h,
                           uint16_t e_type,
                           uint16_t e_machine,
                           uint64_t e_entry,
                           uint16_t e_phnum,
                           uint16_t e_shnum)
{
    memset(h, 0, sizeof(*h));
    h->e_ident[0] = 0x7f;
    h->e_ident[1] = 'E';
    h->e_ident[2] = 'L';
    h->e_ident[3] = 'F';
    h->e_ident[4] = 2;   /* ELFCLASS64 */
    h->e_ident[5] = 1;   /* ELFDATA2LSB */
    h->e_ident[6] = 1;   /* EV_CURRENT */
    h->e_type      = e_type;
    h->e_machine   = e_machine;
    h->e_version   = 1;
    h->e_entry     = e_entry;
    h->e_ehsize    = (uint16_t)sizeof(Elf64_Ehdr);
    h->e_phnum     = e_phnum;
    h->e_shnum     = e_shnum;
}

/* Записать буфер в временный файл через mkstemp, вернуть путь (нужно free) */
static char *write_tmpfile(const void *data, size_t len)
{
    char *path = malloc(32);
    if (!path) return NULL;
    memcpy(path, "/tmp/test_elf_XXXXXX", 21);
    int fd = mkstemp(path);
    if (fd < 0) { free(path); return NULL; }
    ssize_t written = write(fd, data, len);
    close(fd);
    if (written < 0 || (size_t)written != len) { unlink(path); free(path); return NULL; }
    return path;
}

int main(void)
{
    printf("=== Тест: elf_parse ===\n");

    elf_info_t info;
    char *path = NULL;

    /* 1. Несуществующий файл возвращает -1 */
    memset(&info, 0, sizeof(info));
    CHECK(elf_parse("/tmp/nonexistent_elf_test_file_xyz", &info) == -1,
          "несуществующий файл возвращает -1");

    /* 2. Пустой файл (/dev/null) возвращает -1 */
    memset(&info, 0, sizeof(info));
    CHECK(elf_parse("/dev/null", &info) == -1,
          "пустой файл возвращает -1");

    /* 3. Текстовый файл (не ELF) возвращает -1 */
    {
        const char *text = "Hello, world!\n";
        char *tpath = write_tmpfile(text, strlen(text));
        memset(&info, 0, sizeof(info));
        int rc = -1;
        if (tpath) { rc = elf_parse(tpath, &info); unlink(tpath); free(tpath); }
        CHECK(rc == -1, "текстовый файл возвращает -1");
    }

    /* Создаём корректный ELF64 LE файл для тестов 4–10 */
    {
        Elf64_Ehdr hdr;
        make_elf64_le(&hdr,
                      2,          /* ET_EXEC */
                      62,         /* EM_X86_64 */
                      0x400000ULL,
                      3,          /* phnum */
                      27);        /* shnum */
        path = write_tmpfile(&hdr, sizeof(hdr));
    }

    /* 4. Корректный ELF64 LE возвращает 0 */
    memset(&info, 0, sizeof(info));
    {
        int rc = path ? elf_parse(path, &info) : -1;
        CHECK(rc == 0, "корректный ELF64 LE возвращает 0");
    }

    /* 5. info.valid == 1 */
    CHECK(info.valid == 1, "info.valid == 1");

    /* 6. info.e_type == ET_EXEC (2) */
    CHECK(info.e_type == 2, "info.e_type == ET_EXEC (2)");

    /* 7. info.e_machine == EM_X86_64 (62) */
    CHECK(info.e_machine == 62, "info.e_machine == EM_X86_64 (62)");

    /* 8. info.e_entry == 0x400000 */
    CHECK(info.e_entry == 0x400000ULL, "info.e_entry == 0x400000");

    /* 9. info.e_phnum == 3 */
    CHECK(info.e_phnum == 3, "info.e_phnum == 3");

    /* 10. info.e_shnum == 27 */
    CHECK(info.e_shnum == 27, "info.e_shnum == 27");

    if (path) { unlink(path); free(path); path = NULL; }

    /* 11. ELF32 (class=1) возвращает -1 */
    {
        Elf64_Ehdr hdr;
        make_elf64_le(&hdr, 2, 62, 0x400000ULL, 3, 27);
        hdr.e_ident[4] = 1; /* ELFCLASS32 */
        char *tpath = write_tmpfile(&hdr, sizeof(hdr));
        memset(&info, 0, sizeof(info));
        int rc = tpath ? elf_parse(tpath, &info) : 0;
        if (tpath) { unlink(tpath); free(tpath); }
        CHECK(rc == -1, "ELF32 (class=1) возвращает -1");
    }

    /* 12. Big-endian ELF (data=2) возвращает -1 */
    {
        Elf64_Ehdr hdr;
        make_elf64_le(&hdr, 2, 62, 0x400000ULL, 3, 27);
        hdr.e_ident[5] = 2; /* ELFDATA2MSB */
        char *tpath = write_tmpfile(&hdr, sizeof(hdr));
        memset(&info, 0, sizeof(info));
        int rc = tpath ? elf_parse(tpath, &info) : 0;
        if (tpath) { unlink(tpath); free(tpath); }
        CHECK(rc == -1, "big-endian ELF (data=2) возвращает -1");
    }

    printf("\nРезультат: %d/%d тестов прошли\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
