#!/bin/sh
# Тест отложенной обработки через workqueue /dev/cppwq (внутри QEMU). Успех = exit 0.

echo "=== K3 01-workqueue ==="

if ! insmod /mnt/share/cppmod.ko; then
    echo "[FAIL] insmod не прошёл"; exit 1
fi
echo "[OK] модуль загружен"

[ -c /dev/cppwq ] || sleep 1
if [ ! -c /dev/cppwq ]; then
    echo "[FAIL] /dev/cppwq не создан (нет misc_register?)"; dmesg | tail -5; exit 1
fi
echo "[OK] /dev/cppwq создан"

# write планирует работу; read (flush_work) дожидается её и отдаёт результат.
echo -n "hello" > /dev/cppwq || { echo "[FAIL] write"; exit 1; }
OUT="$(cat /dev/cppwq)"
if [ "$OUT" != "HELLO" ]; then
    echo "[FAIL] read вернул '$OUT' (ждали 'HELLO' — отложенная работа не сработала?)"; exit 1
fi
echo "[OK] отложенная обработка (workqueue): '$OUT'"

echo -n "Wq-42" > /dev/cppwq
OUT2="$(cat /dev/cppwq)"
if [ "$OUT2" != "WQ-42" ]; then
    echo "[FAIL] перезапись: '$OUT2' (ждали 'WQ-42')"; exit 1
fi
echo "[OK] перезапись через workqueue: '$OUT2'"

rmmod cppmod 2>/dev/null || true
echo "ALL PASS"
exit 0
