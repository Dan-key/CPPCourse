#!/bin/sh
echo "=== K7 01-kprobe-count ==="
mount -t proc none /proc 2>/dev/null
insmod /mnt/share/cppmod.ko || { echo "[FAIL] insmod"; dmesg|tail -5; exit 1; }
[ -f /proc/k7_kprobe ] || { echo "[FAIL] нет /proc/k7_kprobe"; rmmod cppmod 2>/dev/null; exit 1; }
C0=$(sed -n 's/.*hits=\([0-9]*\).*/\1/p' /proc/k7_kprobe)
# сгенерировать открытия файлов (каждое идёт через do_sys_openat2):
for i in 1 2 3 4 5; do cat /proc/version >/dev/null 2>&1; done
C1=$(sed -n 's/.*hits=\([0-9]*\).*/\1/p' /proc/k7_kprobe)
echo "hits: ${C0:-?} -> ${C1:-?}"
if [ -n "$C1" ] && [ "$C1" -gt "${C0:-0}" ]; then
	echo "[OK] kprobe считает вызовы (hits выросли)"
else
	echo "[FAIL] счётчик не вырос"; rmmod cppmod 2>/dev/null; exit 1
fi
rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod"; exit 1; }
[ -f /proc/k7_kprobe ] && { echo "[FAIL] /proc не удалён после rmmod"; exit 1; }
dmesg | grep -qiE 'BUG:|Oops|call trace|scheduling while atomic' && { echo "[FAIL] ядро ругается"; dmesg|grep -iE 'BUG:|Oops'|tail -5; exit 1; }
echo "ALL PASS"; exit 0
