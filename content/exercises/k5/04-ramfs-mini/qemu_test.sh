#!/bin/sh
echo "=== K5 04-ramfs-mini ==="
insmod /mnt/share/cppmod.ko || { echo "[FAIL] insmod"; dmesg|tail -5; exit 1; }
grep -qw k5fs /proc/filesystems || { echo "[FAIL] k5fs не зарегистрирована"; rmmod cppmod 2>/dev/null; exit 1; }
mkdir -p /mnt/k5
mount -t k5fs none /mnt/k5 || { echo "[FAIL] mount k5fs"; dmesg|tail -5; rmmod cppmod 2>/dev/null; exit 1; }
[ -d /mnt/k5 ] || { echo "[FAIL] корень не каталог"; umount /mnt/k5 2>/dev/null; rmmod cppmod 2>/dev/null; exit 1; }
MT=$(stat -f -c %t /mnt/k5 2>/dev/null)
umount /mnt/k5 || { echo "[FAIL] umount"; rmmod cppmod 2>/dev/null; exit 1; }
rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod (ФС ещё смонтирована?)"; exit 1; }
dmesg | grep -qiE 'BUG:|Oops|call trace|WARNING:' && { echo "[FAIL] ядро ругается"; dmesg|grep -iE 'BUG:|Oops|WARNING:'|tail -5; exit 1; }
echo "ALL PASS"; exit 0
