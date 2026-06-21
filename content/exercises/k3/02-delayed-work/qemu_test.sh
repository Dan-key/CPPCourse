#!/bin/sh
# Тест отложенной работы через delayed_work /dev/cppdw (внутри QEMU). Успех = exit 0.

echo "=== K3 02-delayed-work ==="

if ! insmod /mnt/share/cppmod.ko; then
    echo "[FAIL] insmod не прошёл"; dmesg | tail -5; exit 1
fi
echo "[OK] модуль загружен"

[ -c /dev/cppdw ] || sleep 1
if [ ! -c /dev/cppdw ]; then
    echo "[FAIL] /dev/cppdw не создан (нет misc_register?)"; dmesg | tail -5; exit 1
fi
echo "[OK] /dev/cppdw создан"

# write планирует delayed_work (через 100 мс); read (flush) дожидается результата.
echo -n "abc" > /dev/cppdw || { echo "[FAIL] write"; exit 1; }
OUT="$(cat /dev/cppdw)"
if [ "$OUT" != "ABC runs=1" ]; then
    echo "[FAIL] read='$OUT' (ждали 'ABC runs=1' — delayed_work не сработала?)"; exit 1
fi
echo "[OK] отложенная работа (delayed_work): '$OUT'"

echo -n "Xy" > /dev/cppdw
OUT2="$(cat /dev/cppdw)"
if [ "$OUT2" != "XY runs=2" ]; then
    echo "[FAIL] read='$OUT2' (ждали 'XY runs=2')"; exit 1
fi
echo "[OK] второй запуск: '$OUT2'"

if dmesg | grep -qiE 'BUG:|Oops|call trace|WARNING:|scheduling while atomic|sleeping function'; then
    echo "[FAIL] ядро ругается:"; dmesg | grep -iE 'BUG:|Oops|WARNING:|sleeping' | tail -5
    rmmod cppmod 2>/dev/null; exit 1
fi

rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod не прошёл"; exit 1; }
echo "ALL PASS"
exit 0
