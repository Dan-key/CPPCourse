#!/bin/sh
# Тест kvmalloc-буфера /dev/cppkv (внутри QEMU). Успех = exit 0.

echo "=== K4 02-kvmalloc-buffer ==="

if ! insmod /mnt/share/cppmod.ko; then
    echo "[FAIL] insmod не прошёл"; dmesg | tail -5; exit 1
fi
[ -c /dev/cppkv ] || sleep 1
if [ ! -c /dev/cppkv ]; then
    echo "[FAIL] /dev/cppkv не создан"; dmesg | tail -5; exit 1
fi
echo "[OK] /dev/cppkv создан"

# Маленький буфер (внутри — kmalloc):
echo -n "hello" > /dev/cppkv || { echo "[FAIL] write"; exit 1; }
OUT="$(cat /dev/cppkv)"
if [ "$OUT" != "hello" ]; then
    echo "[FAIL] read='$OUT' (ждали 'hello')"; rmmod cppmod 2>/dev/null; exit 1
fi
echo "[OK] маленький буфер: '$OUT'"

# Крупный буфер ОДНИМ write() (внутри kvmalloc может пойти по vmalloc):
dd if=/dev/zero of=/dev/cppkv bs=262144 count=1 2>/dev/null || { echo "[FAIL] большой write"; rmmod cppmod 2>/dev/null; exit 1; }
SZ="$(dd if=/dev/cppkv bs=262144 count=1 2>/dev/null | wc -c)"
if [ "$SZ" != "262144" ]; then
    echo "[FAIL] большой буфер: прочитано $SZ (ждали 262144 — kvmalloc не масштабируется?)"
    rmmod cppmod 2>/dev/null; exit 1
fi
echo "[OK] большой буфер: $SZ байт"

if dmesg | grep -qiE 'BUG:|Oops|call trace|WARNING:|use-after-free'; then
    echo "[FAIL] ядро ругается:"; dmesg | grep -iE 'BUG:|Oops|WARNING:' | tail -5
    rmmod cppmod 2>/dev/null; exit 1
fi

rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod не прошёл"; exit 1; }
echo "ALL PASS"
exit 0
