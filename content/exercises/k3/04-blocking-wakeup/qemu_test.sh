#!/bin/sh
# Тест пробуждения блокирующего read из отложенной работы /dev/cppwake (QEMU). Успех=exit 0.

echo "=== K3 04-blocking-wakeup ==="

if ! insmod /mnt/share/cppmod.ko; then
    echo "[FAIL] insmod не прошёл"; dmesg | tail -5; exit 1
fi
echo "[OK] модуль загружен"

[ -c /dev/cppwake ] || sleep 1
if [ ! -c /dev/cppwake ]; then
    echo "[FAIL] /dev/cppwake не создан (нет misc_register?)"; dmesg | tail -5; exit 1
fi
echo "[OK] /dev/cppwake создан"

# Блокирующее чтение: читатель в фоне спит в wait_event (готовности ещё нет),
# write планирует работу → work_fn uppercase + wake_up → читатель просыпается.
# dd bs=64 count=1 = РОВНО один read() (head -c сделал бы второй read() и завис).
( dd if=/dev/cppwake bs=64 count=1 2>/dev/null | tr -d '\n' > /tmp/wkout ) &
RPID=$!
sleep 1                                    # читатель сейчас спит (data_ready=0)
echo -n "ping" > /dev/cppwake              # событие → schedule_work → wake_up
wait "$RPID"
OUT="$(cat /tmp/wkout)"
echo "[i] блокирующий read получил: '$OUT'"
if [ "$OUT" != "PING" ]; then
    echo "[FAIL] read='$OUT' (ждали 'PING' — wake/work из bottom half сломаны?)"
    rmmod cppmod 2>/dev/null; exit 1
fi
echo "[OK] read проснулся по wake_up и получил обработанный результат"

if dmesg | grep -qiE 'BUG:|Oops|call trace|WARNING:|scheduling while atomic|sleeping function'; then
    echo "[FAIL] ядро ругается:"; dmesg | grep -iE 'BUG:|Oops|WARNING:|sleeping' | tail -5
    rmmod cppmod 2>/dev/null; exit 1
fi

rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod не прошёл"; exit 1; }
echo "ALL PASS"
exit 0
