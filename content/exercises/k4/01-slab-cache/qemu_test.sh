#!/bin/sh
# Тест slab-кэша /dev/cppslab (внутри QEMU). Успех = exit 0.

echo "=== K4 01-slab-cache ==="

if ! insmod /mnt/share/cppmod.ko; then
    echo "[FAIL] insmod не прошёл (kmem_cache_create вернул NULL?)"; dmesg | tail -5; exit 1
fi
echo "[OK] модуль загружен"

[ -c /dev/cppslab ] || sleep 1
if [ ! -c /dev/cppslab ]; then
    echo "[FAIL] /dev/cppslab не создан"; dmesg | tail -5; exit 1
fi
echo "[OK] /dev/cppslab создан"

echo "alpha" > /dev/cppslab || { echo "[FAIL] write alpha"; exit 1; }
echo "beta"  > /dev/cppslab || { echo "[FAIL] write beta"; exit 1; }
OUT="$(cat /dev/cppslab)"
if [ "$OUT" != "alpha beta" ]; then
    echo "[FAIL] read='$OUT' (ждали 'alpha beta' — kmem_cache_alloc/список не работают?)"
    rmmod cppmod 2>/dev/null; exit 1
fi
echo "[OK] объекты из кэша: '$OUT'"

echo "gamma" > /dev/cppslab
OUT2="$(cat /dev/cppslab)"
if [ "$OUT2" != "alpha beta gamma" ]; then
    echo "[FAIL] read='$OUT2' (ждали 'alpha beta gamma')"; rmmod cppmod 2>/dev/null; exit 1
fi
echo "[OK] добавление: '$OUT2'"

# Реальные проблемы, но не загрузочные баннеры (boot печатает "slab"/"kmemleak ... initialized").
if dmesg | grep -qiE 'BUG:|Oops|call trace|WARNING:|use-after-free|still has objects'; then
    echo "[FAIL] ядро ругается:"; dmesg | grep -iE 'BUG:|Oops|WARNING:|use-after-free|still has' | tail -5
    rmmod cppmod 2>/dev/null; exit 1
fi

rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod (объекты не освобождены до destroy?)"; exit 1; }
echo "ALL PASS"
exit 0
