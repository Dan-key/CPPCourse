#!/bin/sh
# Тест /dev/cppmax (внутри QEMU). Успех = exit 0.

echo "=== K2 01-atomic-maxtrack ==="

if ! insmod /mnt/share/cppmod.ko; then
    echo "[FAIL] insmod не прошёл"; dmesg | tail -5; exit 1
fi
echo "[OK] модуль загружен"

[ -c /dev/cppmax ] || sleep 1
if [ ! -c /dev/cppmax ]; then
    echo "[FAIL] /dev/cppmax не создан"; dmesg | tail -5; exit 1
fi

check() {  # $1 = ожидаемая строка
    got="$(cat /dev/cppmax)"
    if [ "$got" != "$1" ]; then
        echo "[FAIL] read='$got', ждали '$1'"; rmmod cppmod 2>/dev/null; exit 1
    fi
    echo "[OK] $got"
}

check "counter=0 max=0"
echo 5  > /dev/cppmax;  check "counter=5 max=5"
echo 3  > /dev/cppmax;  check "counter=8 max=8"
echo -10 > /dev/cppmax; check "counter=-2 max=8"     # max НЕ должен упасть
echo 20 > /dev/cppmax;  check "counter=18 max=18"    # новый максимум

# Проверка ядра на ругань (sleeping while atomic, oops и т.п.)
if dmesg | grep -qiE 'BUG:|Oops|WARNING:|scheduling while atomic'; then
    echo "[FAIL] ядро ругается:"; dmesg | grep -iE 'BUG:|Oops|WARNING:' | tail -5
    rmmod cppmod 2>/dev/null; exit 1
fi

rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod не прошёл"; exit 1; }
echo "ALL PASS"
exit 0
