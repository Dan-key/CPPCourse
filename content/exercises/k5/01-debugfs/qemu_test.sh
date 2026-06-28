#!/bin/sh
echo "=== K5 01-debugfs ==="
mount -t debugfs none /sys/kernel/debug 2>/dev/null
insmod /mnt/share/cppmod.ko || { echo "[FAIL] insmod"; dmesg|tail -5; exit 1; }
[ -d /sys/kernel/debug/k5_debug ] || { echo "[FAIL] нет каталога k5_debug"; rmmod cppmod 2>/dev/null; exit 1; }
echo 42 > /sys/kernel/debug/k5_debug/counter
VAL=$(cat /sys/kernel/debug/k5_debug/counter)
[ "$VAL" = "42" ] || { echo "[FAIL] counter='$VAL' (ждали 42)"; rmmod cppmod 2>/dev/null; exit 1; }
ST=$(cat /sys/kernel/debug/k5_debug/status)
[ "$ST" = "Driver is active" ] || { echo "[FAIL] status='$ST'"; rmmod cppmod 2>/dev/null; exit 1; }
rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod"; exit 1; }
[ -d /sys/kernel/debug/k5_debug ] && { echo "[FAIL] каталог не удалён"; exit 1; }
dmesg | grep -qiE 'BUG:|Oops|call trace|WARNING:' && { echo "[FAIL] ядро ругается"; dmesg|grep -iE 'BUG:|Oops|WARNING:'|tail -5; exit 1; }
echo "ALL PASS"; exit 0
