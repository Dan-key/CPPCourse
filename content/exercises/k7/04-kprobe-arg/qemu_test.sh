#!/bin/sh
echo "=== K7 04-kprobe-arg ==="
mount -t proc none /proc 2>/dev/null
insmod /mnt/share/cppmod.ko || { echo "[FAIL] insmod"; dmesg|tail -5; exit 1; }
[ -f /proc/k7_arg ] || { echo "[FAIL] нет /proc/k7_arg"; rmmod cppmod 2>/dev/null; exit 1; }
C0=$(sed -n 's/.*cwd_opens=\([0-9]*\).*/\1/p' /proc/k7_arg)
# открытия по абсолютному пути от cwd → dfd == AT_FDCWD:
for i in 1 2 3 4 5; do cat /proc/version >/dev/null 2>&1; done
C1=$(sed -n 's/.*cwd_opens=\([0-9]*\).*/\1/p' /proc/k7_arg)
echo "cwd_opens: ${C0:-?} -> ${C1:-?}"
if [ -n "$C1" ] && [ "$C1" -gt "${C0:-0}" ]; then
	echo "[OK] kprobe считал аргумент dfd и отфильтровал AT_FDCWD"
else
	echo "[FAIL] счётчик AT_FDCWD не вырос"; rmmod cppmod 2>/dev/null; exit 1
fi
rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod"; exit 1; }
dmesg | grep -qiE 'BUG:|Oops|call trace|scheduling while atomic' && { echo "[FAIL] ядро ругается"; dmesg|grep -iE 'BUG:|Oops'|tail -5; exit 1; }
echo "ALL PASS"; exit 0
