#!/bin/sh
echo "=== K6 02-nf-port-filter ==="
ip link set lo up 2>/dev/null || ifconfig lo up 2>/dev/null
# грузим с параметром: блокируем порт 9999
insmod /mnt/share/cppmod.ko block_port=9999 || { echo "[FAIL] insmod"; dmesg|tail -5; exit 1; }
# слушатели: 9999 (должен быть недоступен), 8888 (должен работать)
nc -l -p 9999 >/dev/null 2>&1 &
nc -l -p 8888 >/dev/null 2>&1 &
sleep 1
# соединение на 9999 → SYN дропается на LOCAL_IN → connect не пройдёт:
if echo hi | nc -w2 127.0.0.1 9999 >/dev/null 2>&1; then
	echo "[FAIL] порт 9999 не заблокирован"; rmmod cppmod 2>/dev/null; exit 1
fi
echo "[OK] порт 9999 заблокирован"
# на 8888 — проходит (другой порт):
if echo hi | nc -w2 127.0.0.1 8888 >/dev/null 2>&1; then
	echo "[OK] порт 8888 открыт"
else
	echo "[FAIL] порт 8888 не должен блокироваться"; rmmod cppmod 2>/dev/null; exit 1
fi
rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod"; exit 1; }
dmesg | grep -qiE 'BUG:|Oops|call trace|WARNING:|scheduling while atomic' && { echo "[FAIL] ядро ругается"; dmesg|grep -iE 'BUG:|Oops|WARNING:'|tail -5; exit 1; }
echo "ALL PASS"; exit 0
