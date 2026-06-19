#!/bin/sh
# Тест /dev/cpppercpu: per-CPU счётчик (внутри QEMU). Успех = exit 0.

echo "=== K2 05-percpu-counter ==="

if ! insmod /mnt/share/cppmod.ko; then
    echo "[FAIL] insmod не прошёл"; dmesg | tail -5; exit 1
fi
[ -c /dev/cpppercpu ] || sleep 1
if [ ! -c /dev/cpppercpu ]; then
    echo "[FAIL] /dev/cpppercpu не создан"; dmesg | tail -5; exit 1
fi

check() {
    got="$(cat /dev/cpppercpu)"
    if [ "$got" != "$1" ]; then
        echo "[FAIL] read='$got', ждали '$1'"; rmmod cppmod 2>/dev/null; exit 1
    fi
    echo "[OK] агрегат = $got"
}

check "0"
echo 5  > /dev/cpppercpu; check "5"        # сумма по ядрам = 5
echo 3  > /dev/cpppercpu; check "8"
echo -2 > /dev/cpppercpu; check "6"

# Много инкрементов — агрегат должен сойтись.
i=0
while [ "$i" -lt 100 ]; do echo 1 > /dev/cpppercpu; i=$((i+1)); done
check "106"

if dmesg | grep -qiE 'BUG:|Oops|WARNING:'; then
    echo "[FAIL] ядро ругается:"; dmesg | grep -iE 'BUG:|Oops|WARNING:' | tail -5
    rmmod cppmod 2>/dev/null; exit 1
fi

rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod не прошёл"; exit 1; }
echo "ALL PASS"
exit 0
