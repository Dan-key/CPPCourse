#!/bin/sh
echo "=== K7 02-kretprobe-latency ==="
mount -t proc none /proc 2>/dev/null
insmod /mnt/share/cppmod.ko || { echo "[FAIL] insmod"; dmesg|tail -5; exit 1; }
[ -f /proc/k7_lat ] || { echo "[FAIL] нет /proc/k7_lat"; rmmod cppmod 2>/dev/null; exit 1; }
# трафик: вызвать целевую функцию открытиями файлов
for i in 1 2 3 4 5 6 7 8; do cat /proc/version >/dev/null 2>&1; done
OUT=$(cat /proc/k7_lat)
echo "lat: $OUT"
CNT=$(echo "$OUT" | sed -n 's/.*count=\([0-9]*\).*/\1/p')
if [ -n "$CNT" ] && [ "$CNT" -gt 0 ]; then
	echo "[OK] kretprobe измерил латентность (count=$CNT)"
else
	echo "[FAIL] count не вырос (count=$CNT)"; rmmod cppmod 2>/dev/null; exit 1
fi
rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod"; exit 1; }
dmesg | grep -qiE 'BUG:|Oops|call trace|scheduling while atomic' && { echo "[FAIL] ядро ругается"; dmesg|grep -iE 'BUG:|Oops'|tail -5; exit 1; }
echo "ALL PASS"; exit 0
