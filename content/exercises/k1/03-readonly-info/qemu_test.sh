#!/bin/sh
# Тест read-only устройства /dev/cppinfo (внутри QEMU). Успех = exit 0.

echo "=== K1 03-readonly-info ==="

if ! insmod /mnt/share/cppmod.ko; then
    echo "[FAIL] insmod не прошёл"; exit 1
fi
echo "[OK] модуль загружен"

[ -c /dev/cppinfo ] || sleep 1
if [ ! -c /dev/cppinfo ]; then
    echo "[FAIL] /dev/cppinfo не создан"; dmesg | tail -5; exit 1
fi
echo "[OK] /dev/cppinfo создан"

OUT="$(cat /dev/cppinfo)"
if [ "$OUT" != "CPPK1-INFO-OK" ]; then
    echo "[FAIL] read вернул '$OUT' (ждали 'CPPK1-INFO-OK')"; exit 1
fi
echo "[OK] read: '$OUT'"

# повторное чтение даёт то же (EOF корректен, не зацикливается)
OUT2="$(cat /dev/cppinfo)"
if [ "$OUT2" != "CPPK1-INFO-OK" ]; then
    echo "[FAIL] повторный read: '$OUT2'"; exit 1
fi
echo "[OK] повторный read стабилен"

# write должен быть ЗАПРЕЩЁН (-EACCES) → echo завершится с ошибкой
if echo -n "x" > /dev/cppinfo 2>/dev/null; then
    echo "[FAIL] write прошёл, а должен был быть запрещён (-EACCES)"; exit 1
fi
echo "[OK] write запрещён (read-only)"

rmmod cppmod 2>/dev/null || true
echo "ALL PASS"
exit 0
