#!/bin/sh
# Тест символьного драйвера /dev/cppchar (выполняется внутри QEMU, busybox sh).
# Успех = exit 0. Прогонщик ищет "__TEST_EXIT_CODE__:0" в выводе ядра.

echo "=== K1 01-char-dev ==="

if ! insmod /mnt/share/cppmod.ko; then
    echo "[FAIL] insmod не прошёл"
    exit 1
fi
echo "[OK] модуль загружен"

# devtmpfs обычно создаёт узел сразу; дадим один запас на всякий случай.
if [ ! -c /dev/cppchar ]; then
    sleep 1
fi
if [ ! -c /dev/cppchar ]; then
    echo "[FAIL] /dev/cppchar не создан (нет регистрации/класса?)"
    dmesg | tail -5
    exit 1
fi
echo "[OK] /dev/cppchar создан"

# write -> read roundtrip
if ! echo -n "hello-kernel" > /dev/cppchar; then
    echo "[FAIL] write не прошёл"
    exit 1
fi
OUT="$(cat /dev/cppchar)"
if [ "$OUT" != "hello-kernel" ]; then
    echo "[FAIL] read вернул '$OUT' (ждали 'hello-kernel')"
    exit 1
fi
echo "[OK] write/read: '$OUT'"

# перезапись новым значением
echo -n "second" > /dev/cppchar
OUT2="$(cat /dev/cppchar)"
if [ "$OUT2" != "second" ]; then
    echo "[FAIL] перезапись: read вернул '$OUT2' (ждали 'second')"
    exit 1
fi
echo "[OK] перезапись: '$OUT2'"

# повторное чтение даёт то же (cat снова с позиции 0)
OUT3="$(cat /dev/cppchar)"
if [ "$OUT3" != "second" ]; then
    echo "[FAIL] повторное чтение: '$OUT3'"
    exit 1
fi
echo "[OK] повторное чтение стабильно: '$OUT3'"

rmmod cppmod 2>/dev/null || true
echo "ALL PASS"
exit 0
