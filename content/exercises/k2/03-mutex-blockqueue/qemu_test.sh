#!/bin/sh
# Тест /dev/cppq: мьютекс + wait queue (внутри QEMU). Успех = exit 0.

echo "=== K2 03-mutex-blockqueue ==="

if ! insmod /mnt/share/cppmod.ko; then
    echo "[FAIL] insmod не прошёл"; dmesg | tail -5; exit 1
fi
[ -c /dev/cppq ] || sleep 1
if [ ! -c /dev/cppq ]; then
    echo "[FAIL] /dev/cppq не создан"; dmesg | tail -5; exit 1
fi

# 1) FIFO-порядок, когда данные уже есть (read не блокирует).
echo "alpha" > /dev/cppq
echo "beta"  > /dev/cppq
A="$(head -c 64 /dev/cppq | tr -d '\n')"
B="$(head -c 64 /dev/cppq | tr -d '\n')"
echo "[i] прочитано: '$A' '$B'"
if [ "$A" != "alpha" ] || [ "$B" != "beta" ]; then
    echo "[FAIL] FIFO нарушен (ждали alpha,beta)"; rmmod cppmod 2>/dev/null; exit 1
fi
echo "[OK] FIFO-порядок верный"

# 2) БЛОКИРУЮЩЕЕ чтение: читатель в фоне ждёт, писатель будит его позже.
( head -c 64 /dev/cppq | tr -d '\n' > /tmp/blkout ) &
RPID=$!
sleep 1                                   # читатель сейчас спит в wait_event (очередь пуста)
echo "delivered" > /dev/cppq              # пишем → wake_up должен разбудить читателя
wait "$RPID"
OUT="$(cat /tmp/blkout)"
echo "[i] блокирующий read получил: '$OUT'"
if [ "$OUT" != "delivered" ]; then
    echo "[FAIL] блокирующий read не получил сообщение (wait/wake сломаны)"
    rmmod cppmod 2>/dev/null; exit 1
fi
echo "[OK] read проснулся по wake_up и получил сообщение"

if dmesg | grep -qiE 'BUG:|Oops|WARNING:|scheduling while atomic|sleeping function'; then
    echo "[FAIL] ядро ругается:"; dmesg | grep -iE 'BUG:|Oops|WARNING:|sleeping' | tail -5
    rmmod cppmod 2>/dev/null; exit 1
fi

rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod не прошёл"; exit 1; }
echo "ALL PASS"
exit 0
