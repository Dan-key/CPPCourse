#!/bin/sh
echo "=== K5 02-procfs ==="
mount -t proc none /proc 2>/dev/null
insmod /mnt/share/cppmod.ko || { echo "[FAIL] insmod"; dmesg|tail -5; exit 1; }
[ -f /proc/k5_list ] || { echo "[FAIL] нет /proc/k5_list"; rmmod cppmod 2>/dev/null; exit 1; }
echo item1 > /proc/k5_list
echo item2 > /proc/k5_list
echo item3 > /proc/k5_list
OUT=$(cat /proc/k5_list)
EXP="item1
item2
item3"
[ "$OUT" = "$EXP" ] || { echo "[FAIL] вывод:"; echo "$OUT"; rmmod cppmod 2>/dev/null; exit 1; }
rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod"; exit 1; }
dmesg | grep -qiE 'BUG:|Oops|call trace|WARNING:' && { echo "[FAIL] ядро ругается"; dmesg|grep -iE 'BUG:|Oops|WARNING:'|tail -5; exit 1; }
echo "ALL PASS"; exit 0
