#!/bin/sh
# Тест misc-устройства /dev/cppupper (внутри QEMU, busybox). Успех = exit 0.

echo "=== K1 02-misc-uppercase ==="

if ! insmod /mnt/share/cppmod.ko; then
    echo "[FAIL] insmod не прошёл"; exit 1
fi
echo "[OK] модуль загружен"

[ -c /dev/cppupper ] || sleep 1
if [ ! -c /dev/cppupper ]; then
    echo "[FAIL] /dev/cppupper не создан (нет misc_register?)"
    dmesg | tail -5; exit 1
fi
echo "[OK] /dev/cppupper создан"

echo -n "hello World 42" > /dev/cppupper || { echo "[FAIL] write"; exit 1; }
OUT="$(cat /dev/cppupper)"
if [ "$OUT" != "HELLO WORLD 42" ]; then
    echo "[FAIL] read вернул '$OUT' (ждали 'HELLO WORLD 42')"; exit 1
fi
echo "[OK] uppercase: '$OUT'"

# цифры и символы не трогаются, буквы апперкейзятся; перезапись
echo -n "abcXYZ-9" > /dev/cppupper
OUT2="$(cat /dev/cppupper)"
if [ "$OUT2" != "ABCXYZ-9" ]; then
    echo "[FAIL] перезапись: '$OUT2' (ждали 'ABCXYZ-9')"; exit 1
fi
echo "[OK] перезапись+upper: '$OUT2'"

rmmod cppmod 2>/dev/null || true
echo "ALL PASS"
exit 0
