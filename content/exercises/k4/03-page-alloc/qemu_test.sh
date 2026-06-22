#!/bin/sh
# Тест прямой страницы buddy /dev/cpppage (внутри QEMU). Успех = exit 0.

echo "=== K4 03-page-alloc ==="

if ! insmod /mnt/share/cppmod.ko; then
    echo "[FAIL] insmod не прошёл"; dmesg | tail -5; exit 1
fi
[ -c /dev/cpppage ] || sleep 1
if [ ! -c /dev/cpppage ]; then
    echo "[FAIL] /dev/cpppage не создан"; dmesg | tail -5; exit 1
fi
echo "[OK] /dev/cpppage создан"

if ! echo -n "pagedata" > /dev/cpppage; then
    echo "[FAIL] write (страница не выделена — get_zeroed_page?)"; rmmod cppmod 2>/dev/null; exit 1
fi
OUT="$(cat /dev/cpppage)"
if [ "$OUT" != "pagedata" ]; then
    echo "[FAIL] read='$OUT' (ждали 'pagedata')"; rmmod cppmod 2>/dev/null; exit 1
fi
echo "[OK] запись/чтение страницы: '$OUT'"

echo -n "second" > /dev/cpppage
OUT2="$(cat /dev/cpppage)"
if [ "$OUT2" != "second" ]; then
    echo "[FAIL] перезапись: '$OUT2' (ждали 'second')"; rmmod cppmod 2>/dev/null; exit 1
fi
echo "[OK] перезапись: '$OUT2'"

if dmesg | grep -qiE 'BUG:|Oops|call trace|WARNING:|bad page|use-after-free'; then
    echo "[FAIL] ядро ругается:"; dmesg | grep -iE 'BUG:|Oops|WARNING:|bad page' | tail -5
    rmmod cppmod 2>/dev/null; exit 1
fi

rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod (free_pages с верным order?)"; exit 1; }
echo "ALL PASS"
exit 0
