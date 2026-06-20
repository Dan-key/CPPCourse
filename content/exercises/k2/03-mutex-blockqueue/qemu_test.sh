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
# dd bs=64 count=1 = РОВНО один read() (head -c 64 сделал бы второй read() и завис
# на пустой очереди — блокирующее чтение никогда не вернёт EOF).
A="$(dd if=/dev/cppq bs=64 count=1 2>/dev/null | tr -d '\n')"
B="$(dd if=/dev/cppq bs=64 count=1 2>/dev/null | tr -d '\n')"
echo "[i] прочитано: '$A' '$B'"
if [ "$A" != "alpha" ] || [ "$B" != "beta" ]; then
    echo "[FAIL] FIFO нарушен (ждали alpha,beta)"; rmmod cppmod 2>/dev/null; exit 1
fi
echo "[OK] FIFO-порядок верный"

# 2) БЛОКИРУЮЩЕЕ чтение: читатель в фоне ждёт, писатель будит его позже.
( dd if=/dev/cppq bs=64 count=1 2>/dev/null | tr -d '\n' > /tmp/blkout ) &
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
