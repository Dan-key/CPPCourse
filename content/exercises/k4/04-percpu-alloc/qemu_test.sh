#!/bin/sh
# Тест динамического per-CPU счётчика /dev/cpppc (внутри QEMU). Успех = exit 0.

echo "=== K4 04-percpu-alloc ==="

if ! insmod /mnt/share/cppmod.ko; then
    echo "[FAIL] insmod не прошёл (alloc_percpu вернул NULL?)"; dmesg | tail -5; exit 1
fi
[ -c /dev/cpppc ] || sleep 1
if [ ! -c /dev/cpppc ]; then
    echo "[FAIL] /dev/cpppc не создан"; dmesg | tail -5; exit 1
fi
echo "[OK] /dev/cpppc создан"

check() {
    got="$(cat /dev/cpppc)"
    if [ "$got" != "$1" ]; then
        echo "[FAIL] read='$got', ждали '$1'"; rmmod cppmod 2>/dev/null; exit 1
    fi
    echo "[OK] агрегат = $got"
}

check "0"
echo 5  > /dev/cpppc; check "5"
echo 3  > /dev/cpppc; check "8"
echo -2 > /dev/cpppc; check "6"

i=0
while [ "$i" -lt 100 ]; do echo 1 > /dev/cpppc; i=$((i+1)); done
check "106"

if dmesg | grep -qiE 'BUG:|Oops|call trace|WARNING:|use-after-free'; then
    echo "[FAIL] ядро ругается:"; dmesg | grep -iE 'BUG:|Oops|WARNING:' | tail -5
    rmmod cppmod 2>/dev/null; exit 1
fi

rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod не прошёл"; exit 1; }
echo "ALL PASS"
exit 0
