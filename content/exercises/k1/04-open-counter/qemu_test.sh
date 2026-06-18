#!/bin/sh
# Тест счётчика открытий /dev/cppcount (внутри QEMU). Успех = exit 0.

echo "=== K1 04-open-counter ==="

if ! insmod /mnt/share/cppmod.ko; then
    echo "[FAIL] insmod не прошёл"; exit 1
fi
echo "[OK] модуль загружен"

[ -c /dev/cppcount ] || sleep 1
if [ ! -c /dev/cppcount ]; then
    echo "[FAIL] /dev/cppcount не создан"; dmesg | tail -5; exit 1
fi
echo "[OK] /dev/cppcount создан"

A="$(cat /dev/cppcount)"   # открытие #1
B="$(cat /dev/cppcount)"   # открытие #2
C="$(cat /dev/cppcount)"   # открытие #3
echo "[i] три чтения: '$A' '$B' '$C'"

if [ "$A" != "1" ]; then echo "[FAIL] первое открытие → '$A' (ждали '1')"; exit 1; fi
if [ "$B" != "2" ]; then echo "[FAIL] второе открытие → '$B' (ждали '2')"; exit 1; fi
if [ "$C" != "3" ]; then echo "[FAIL] третье открытие → '$C' (ждали '3')"; exit 1; fi
echo "[OK] счётчик открытий растёт: 1,2,3"

rmmod cppmod 2>/dev/null || true
echo "ALL PASS"
exit 0
