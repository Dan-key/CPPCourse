#!/bin/sh
echo "=== K6 03-nf-log-stats ==="
mount -t proc none /proc 2>/dev/null
ip link set lo up 2>/dev/null || ifconfig lo up 2>/dev/null
insmod /mnt/share/cppmod.ko || { echo "[FAIL] insmod"; dmesg|tail -5; exit 1; }
[ -f /proc/k6_stats ] || { echo "[FAIL] нет /proc/k6_stats"; rmmod cppmod 2>/dev/null; exit 1; }
# сгенерировать ICMP: 3 echo + 3 reply пройдут LOCAL_IN по loopback
ping -c3 -W1 127.0.0.1 >/dev/null 2>&1
OUT=$(cat /proc/k6_stats)
echo "stats: $OUT"
ICMP=$(echo "$OUT" | sed -n 's/.*icmp=\([0-9]*\).*/\1/p')
if [ -n "$ICMP" ] && [ "$ICMP" -ge 3 ]; then
	echo "[OK] icmp=$ICMP (счётчик растёт)"
else
	echo "[FAIL] icmp счётчик не вырос (icmp=$ICMP)"; rmmod cppmod 2>/dev/null; exit 1
fi
rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod"; exit 1; }
[ -f /proc/k6_stats ] && { echo "[FAIL] /proc/k6_stats не удалён после rmmod"; exit 1; }
dmesg | grep -qiE 'BUG:|Oops|call trace|WARNING:|scheduling while atomic' && { echo "[FAIL] ядро ругается"; dmesg|grep -iE 'BUG:|Oops|WARNING:'|tail -5; exit 1; }
echo "ALL PASS"; exit 0
