#!/bin/sh
# Тест /dev/cppstack (внутри QEMU). Успех = exit 0.

echo "=== K2 02-spinlock-stack ==="

if ! insmod /mnt/share/cppmod.ko; then
    echo "[FAIL] insmod не прошёл"; dmesg | tail -5; exit 1
fi
[ -c /dev/cppstack ] || sleep 1
if [ ! -c /dev/cppstack ]; then
    echo "[FAIL] /dev/cppstack не создан"; dmesg | tail -5; exit 1
fi

check() {
    got="$(cat /dev/cppstack)"
    if [ "$got" != "$1" ]; then
        echo "[FAIL] read='$got', ждали '$1'"; rmmod cppmod 2>/dev/null; exit 1
    fi
    echo "[OK] $got"
}

check ""                                   # пустой стек → пустая строка
echo "push 1" > /dev/cppstack
echo "push 2" > /dev/cppstack
echo "push 3" > /dev/cppstack
check "3 2 1"                              # LIFO: вершина первой
echo "pop" > /dev/cppstack
check "2 1"
echo "push 9" > /dev/cppstack
check "9 2 1"
echo "pop" > /dev/cppstack
echo "pop" > /dev/cppstack
echo "pop" > /dev/cppstack
check ""                                   # снова пусто

if dmesg | grep -qiE 'BUG:|Oops|WARNING:|scheduling while atomic|sleeping function'; then
    echo "[FAIL] ядро ругается:"; dmesg | grep -iE 'BUG:|Oops|WARNING:|sleeping' | tail -5
    rmmod cppmod 2>/dev/null; exit 1
fi

rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod не прошёл"; exit 1; }
echo "ALL PASS"
exit 0
