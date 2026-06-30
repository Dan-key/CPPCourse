#!/bin/sh
echo "=== K7 03-trace-printk ==="
mount -t proc none /proc 2>/dev/null
mount -t tracefs nodev /sys/kernel/tracing 2>/dev/null
TR=/sys/kernel/tracing/trace
[ -f "$TR" ] || TR=/sys/kernel/debug/tracing/trace   # старый путь
insmod /mnt/share/cppmod.ko || { echo "[FAIL] insmod"; dmesg|tail -5; exit 1; }
[ -e /proc/k7_trace ] || { echo "[FAIL] нет /proc/k7_trace"; rmmod cppmod 2>/dev/null; exit 1; }
echo 1 > "$(dirname "$TR")/tracing_on" 2>/dev/null
# записать маркер через свой /proc → должен уйти в ftrace-буфер:
echo "K7MARKER42" > /proc/k7_trace
if grep -q K7MARKER42 "$TR" 2>/dev/null; then
	echo "[OK] маркер виден в ftrace-буфере ($TR)"
else
	echo "[FAIL] маркер K7MARKER42 не найден в $TR"; head -20 "$TR" 2>/dev/null; rmmod cppmod 2>/dev/null; exit 1
fi
# и НЕ должен был уйти в dmesg как обычный лог:
dmesg | grep -q K7MARKER42 && echo "[WARN] маркер попал и в dmesg — это похоже на printk, а не trace_printk"
rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod"; exit 1; }
dmesg | grep -qiE 'BUG:|Oops|call trace|scheduling while atomic' && { echo "[FAIL] ядро ругается"; dmesg|grep -iE 'BUG:|Oops'|tail -5; exit 1; }
echo "ALL PASS"; exit 0
