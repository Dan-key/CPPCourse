#!/bin/sh
echo "=== K5 03-sysfs ==="
mount -t sysfs none /sys 2>/dev/null
insmod /mnt/share/cppmod.ko || { echo "[FAIL] insmod"; dmesg|tail -5; exit 1; }
F=/sys/kernel/k5_device/mode_level
[ -f "$F" ] || { echo "[FAIL] нет $F"; rmmod cppmod 2>/dev/null; exit 1; }
echo 777 > "$F"
VAL=$(cat "$F")
[ "$VAL" = "777" ] || { echo "[FAIL] mode_level='$VAL' (ждали 777)"; rmmod cppmod 2>/dev/null; exit 1; }
if echo bad_value > "$F" 2>/dev/null; then echo "[FAIL] запись не-числа должна давать ошибку"; rmmod cppmod 2>/dev/null; exit 1; fi
rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod"; exit 1; }
[ -d /sys/kernel/k5_device ] && { echo "[FAIL] каталог не удалён"; exit 1; }
dmesg | grep -qiE 'BUG:|Oops|call trace|WARNING:' && { echo "[FAIL] ядро ругается"; dmesg|grep -iE 'BUG:|Oops|WARNING:'|tail -5; exit 1; }
echo "ALL PASS"; exit 0
