#!/bin/sh
echo "=== K6 04-nf-modify-ttl ==="
ip link set lo up 2>/dev/null || ifconfig lo up 2>/dev/null
# базовый TTL без модуля (на loopback обычно 64):
BASE=$(ping -c1 -W1 127.0.0.1 2>/dev/null | grep -o 'ttl=[0-9]*' | head -1)
echo "базовый $BASE"
insmod /mnt/share/cppmod.ko || { echo "[FAIL] insmod"; dmesg|tail -5; exit 1; }
# хук на LOCAL_OUT уменьшает TTL ответа на 1 → ping видит ttl=63 (и пакет ПРИНЯТ → чексумма верна)
OUT=$(ping -c1 -W1 127.0.0.1 2>/dev/null)
if echo "$OUT" | grep -q 'ttl=63'; then
	echo "[OK] TTL уменьшен до 63, пакет принят (чексумма корректна)"
else
	echo "[FAIL] ожидался ttl=63:"; echo "$OUT" | grep -o 'ttl=[0-9]*'; rmmod cppmod 2>/dev/null; exit 1
fi
rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod"; exit 1; }
# после выгрузки TTL снова базовый:
ping -c1 -W1 127.0.0.1 2>/dev/null | grep -q 'ttl=64' || echo "[WARN] базовый TTL не 64 (не критично)"
dmesg | grep -qiE 'BUG:|Oops|call trace|WARNING:|scheduling while atomic' && { echo "[FAIL] ядро ругается"; dmesg|grep -iE 'BUG:|Oops|WARNING:'|tail -5; exit 1; }
echo "ALL PASS"; exit 0
