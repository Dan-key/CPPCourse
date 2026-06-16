# Задание: read_whole_file с goto-cleanup

Реализуй функцию чтения файла целиком с обязательным использованием идиомы goto-cleanup.

## Интерфейс

```c
/*
 * Открыть файл по path, прочитать всё содержимое, вернуть указатель на буфер.
 * *out_len — размер содержимого в байтах (без нуль-терминатора).
 * При ошибке: вернуть NULL, утечек ресурсов быть не должно.
 * Caller takes ownership of returned buffer — должен вызвать free().
 */
char *read_whole_file(const char *path, size_t *out_len);
```

## Алгоритм

1. `fopen(path, "rb")`  
2. `fseek(f, 0, SEEK_END)` + `ftell(f)` — получить размер  
3. `rewind(f)`  
4. `malloc(size + 1)` — +1 для нуль-терминатора  
5. `fread(buf, 1, size, f)`  
6. Добавить `'\0'` в конец  
7. Вернуть `buf`, записать `*out_len = size`

## goto-cleanup обязателен

```c
char *read_whole_file(const char *path, size_t *out_len) {
    char *buf = NULL;
    FILE *f   = NULL;
    // ...

out_close:   fclose(f);
out_free:    free(buf);
out:         return NULL;  // или return buf при успехе
}
```

## Крайние случаи

- Несуществующий файл → NULL, нет утечек
- Пустой файл (0 байт) → вернуть валидный буфер с '\0', *out_len = 0
- fread вернул меньше size байт → ошибка, NULL

## Запуск тестов

Тесты создают временные файлы и запускают под ASan (через sandbox).
