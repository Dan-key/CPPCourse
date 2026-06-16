#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

typedef struct {
    long file_count;
    long dir_count;
    long symlink_count;
    long total_size;
} dir_stats_t;

int  collect_stats(const char *path, dir_stats_t *stats);
long find_largest_file(const char *dir_path, char *name_buf, size_t buf_size);

static int g_run = 0, g_pass = 0;

#define CHECK(cond, msg) do { \
    g_run++; \
    if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } \
    else       { printf("  [FAIL] %s\n", msg); } \
} while(0)

/* Создать файл с заданным содержимым */
static void write_file(const char *path, const char *content, size_t len) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    ssize_t w = write(fd, content, len);
    (void)w;
    close(fd);
}

/* Рекурсивно удалить директорию (упрощённо через system) */
static void rmrf(const char *path) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
    int r = system(cmd);
    (void)r;
}

int main(void) {
    /* Создать временную директорию */
    char base[] = "/tmp/cppcourse_dirstats_XXXXXX";
    char *tmpdir = mkdtemp(base);
    if (!tmpdir) { fprintf(stderr, "mkdtemp failed\n"); return 1; }

    /* ---- 1: пустая директория ---- */
    printf("=== Пустая директория ===\n");
    {
        char emptydir[512];
        snprintf(emptydir, sizeof(emptydir), "%s/empty", tmpdir);
        mkdir(emptydir, 0755);
        dir_stats_t s = {0, 0, 0, 0};
        int r = collect_stats(emptydir, &s);
        CHECK(r == 0, "collect_stats пустой дир: возвращает 0");
        CHECK(s.file_count == 0 && s.dir_count == 0 &&
              s.symlink_count == 0 && s.total_size == 0,
              "collect_stats пустой дир: все счётчики 0");
    }

    /* Создать структуру:
       tmpdir/
         a.txt  (100 байт)
         b.txt  (200 байт)
         c.txt  (300 байт)
         subdir/
           d.txt (50 байт)
         link -> a.txt (symlink)
    */
    char buf100[101], buf200[201], buf300[301], buf50[51];
    memset(buf100, 'A', 100); buf100[100] = 0;
    memset(buf200, 'B', 200); buf200[200] = 0;
    memset(buf300, 'C', 300); buf300[300] = 0;
    memset(buf50,  'D', 50);  buf50[50]   = 0;

    char atxt[512], btxt[512], ctxt[512], subdir[512], dtxt[512], lnk[512];
    snprintf(atxt,   sizeof(atxt),   "%s/a.txt",    tmpdir);
    snprintf(btxt,   sizeof(btxt),   "%s/b.txt",    tmpdir);
    snprintf(ctxt,   sizeof(ctxt),   "%s/c.txt",    tmpdir);
    snprintf(subdir, sizeof(subdir), "%s/subdir",   tmpdir);
    snprintf(dtxt,   sizeof(dtxt),   "%s/subdir/d.txt", tmpdir);
    snprintf(lnk,    sizeof(lnk),    "%s/link",     tmpdir);

    write_file(atxt, buf100, 100);
    write_file(btxt, buf200, 200);
    write_file(ctxt, buf300, 300);
    mkdir(subdir, 0755);
    write_file(dtxt, buf50, 50);
    symlink(atxt, lnk);

    /* ---- 2-4: collect_stats на tmpdir ---- */
    printf("=== collect_stats с файлами ===\n");
    {
        dir_stats_t s = {0, 0, 0, 0};
        int r = collect_stats(tmpdir, &s);
        CHECK(r == 0, "collect_stats возвращает 0");
        CHECK(s.file_count == 4, "file_count == 4 (a,b,c,d рекурсивно)");
        CHECK(s.total_size == 650, "total_size == 650 (100+200+300+50)");
        CHECK(s.dir_count == 1, "dir_count == 1 (subdir)");
        CHECK(s.symlink_count == 1, "symlink_count == 1 (link)");
    }

    /* ---- 5-6: find_largest_file (нерекурсивно в tmpdir) ---- */
    printf("=== find_largest_file ===\n");
    {
        char name[256] = {0};
        long sz = find_largest_file(tmpdir, name, sizeof(name));
        CHECK(sz == 300, "find_largest_file: размер == 300");
        CHECK(strcmp(name, "c.txt") == 0, "find_largest_file: имя == 'c.txt'");
    }

    /* ---- 7: find_largest_file на пустой директории ---- */
    {
        char emptydir2[512];
        snprintf(emptydir2, sizeof(emptydir2), "%s/empty2", tmpdir);
        mkdir(emptydir2, 0755);
        char name[256] = {0};
        long sz = find_largest_file(emptydir2, name, sizeof(name));
        CHECK(sz == 0, "find_largest_file пустой дир: 0");
    }

    /* ---- 8: collect_stats на несуществующем пути ---- */
    printf("=== Несуществующий путь ===\n");
    {
        dir_stats_t s = {0, 0, 0, 0};
        int r = collect_stats("/tmp/cppcourse_nonexistent_xyz_99999", &s);
        CHECK(r == -1, "collect_stats несущ. путь: -1");
    }

    /* ---- 9: рекурсия — файлы в поддиректории считаются ---- */
    printf("=== Рекурсия ===\n");
    {
        /* Создать вложенную структуру */
        char nested[512], nfile[512];
        snprintf(nested, sizeof(nested), "%s/subdir/nested", tmpdir);
        snprintf(nfile,  sizeof(nfile),  "%s/subdir/nested/n.txt", tmpdir);
        mkdir(nested, 0755);
        write_file(nfile, "hi", 2);

        dir_stats_t s = {0, 0, 0, 0};
        collect_stats(tmpdir, &s);
        CHECK(s.file_count == 5, "рекурсия: file_count включает вложенный файл");
        CHECK(s.dir_count == 2, "рекурсия: dir_count == 2 (subdir + nested)");
    }

    /* ---- 10: symlinks не разыменовываются (lstat, не stat) ---- */
    printf("=== Symlink не разыменовывается ===\n");
    {
        /* 'link' -> atxt (100 байт), но size symlink ≠ 100 */
        struct stat lstat_st, stat_st;
        lstat(lnk, &lstat_st);
        stat(lnk, &stat_st);
        /* Если collect_stats использует lstat, total_size не включит 100 от symlink */
        dir_stats_t s = {0, 0, 0, 0};
        collect_stats(tmpdir, &s);
        /* total_size должен быть 100+200+300+50+2 = 652, symlink не добавляет свои данные */
        CHECK(s.symlink_count > 0, "symlinks детектируются (symlink_count > 0)");
        /* Главное: lstat используется — symlink не считается как файл */
        CHECK(s.file_count == 5 && s.symlink_count == 1,
              "file_count и symlink_count разделены корректно");
    }

    /* ---- 11: find_largest_file на несуществующей директории ---- */
    {
        char name[256] = {0};
        long sz = find_largest_file("/tmp/cppcourse_nodir_xyz", name, sizeof(name));
        CHECK(sz == -1, "find_largest_file несущ. дир: -1");
    }

    /* ---- 12: сумма счётчиков ---- */
    printf("=== Сумма счётчиков ===\n");
    {
        /* В subdir: d.txt, nested/, nested/n.txt */
        dir_stats_t s = {0, 0, 0, 0};
        collect_stats(subdir, &s);
        long total = s.file_count + s.dir_count + s.symlink_count;
        /* subdir содержит: d.txt (file), nested/ (dir), nested/n.txt (file) */
        CHECK(total == 3, "file_count+dir_count+symlink_count в subdir == 3");
    }

    rmrf(tmpdir);

    printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
