#!/bin/sh
echo "=== K6 01-nf-drop ==="
ip link set lo up 2>/dev/null || ifconfig lo up 2>/dev/null
# до модуля loopback-ping идёт (если нет — среда не готова, но продолжим):
ping -c1 -W1 127.0.0.1 >/dev/null 2>&1 || echo "[WARN] ping не идёт даже без модуля"
insmod /mnt/share/cppmod.ko || { echo "[FAIL] insmod"; dmesg|tail -5; exit 1; }
# после загрузки ICMP должен ДРОПАТЬСЯ → ping не проходит:
if ping -c1 -W1 127.0.0.1 >/dev/null 2>&1; then
	echo "[FAIL] ICMP не дропается — ping прошёл"; rmmod cppmod 2>/dev/null; exit 1
fi
echo "[OK] ICMP дропается (ping не прошёл)"
rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod"; exit 1; }
# после выгрузки ping снова идёт:
ping -c1 -W1 127.0.0.1 >/dev/null 2>&1 || { echo "[FAIL] ping не вернулся после rmmod"; exit 1; }
echo "[OK] после rmmod ICMP снова проходит"
dmesg | grep -qiE 'BUG:|Oops|call trace|WARNING:|scheduling while atomic' && { echo "[FAIL] ядро ругается"; dmesg|grep -iE 'BUG:|Oops|WARNING:'|tail -5; exit 1; }
echo "ALL PASS"; exit 0
