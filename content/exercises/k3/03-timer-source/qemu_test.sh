#!/bin/sh
# Тест таймера-источника /dev/cpptimer (внутри QEMU). Успех = exit 0.

echo "=== K3 03-timer-source ==="

if ! insmod /mnt/share/cppmod.ko; then
    echo "[FAIL] insmod не прошёл"; dmesg | tail -5; exit 1
fi
echo "[OK] модуль загружен"

[ -c /dev/cpptimer ] || sleep 1
if [ ! -c /dev/cpptimer ]; then
    echo "[FAIL] /dev/cpptimer не создан (нет misc_register?)"; dmesg | tail -5; exit 1
fi
echo "[OK] /dev/cpptimer создан"

A="$(cat /dev/cpptimer)"
ta="${A#ticks=}"; ta="${ta%% *}"           # число тиков из "ticks=N processed=M"
echo "[i] первый снимок: '$A'"

sleep 1                                     # таймер тикает каждые 50 мс → ~20 тиков

B="$(cat /dev/cpptimer)"
tb="${B#ticks=}"; tb="${tb%% *}"
pb="${B##*processed=}"
echo "[i] второй снимок: '$B'"

if ! [ "$tb" -gt "$ta" ] 2>/dev/null; then
    echo "[FAIL] таймер не тикает: ticks $ta -> $tb (ждали рост)"; rmmod cppmod 2>/dev/null; exit 1
fi
echo "[OK] таймер тикает: $ta -> $tb"

if ! [ "$pb" -gt 0 ] 2>/dev/null; then
    echo "[FAIL] processed=$pb (work не выполнялась в process context?)"; rmmod cppmod 2>/dev/null; exit 1
fi
echo "[OK] отложенная работа выполнялась: processed=$pb"

if dmesg | grep -qiE 'BUG:|Oops|call trace|WARNING:|scheduling while atomic|sleeping function'; then
    echo "[FAIL] ядро ругается:"; dmesg | grep -iE 'BUG:|Oops|WARNING:|sleeping' | tail -5
    rmmod cppmod 2>/dev/null; exit 1
fi

rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod не прошёл (порядок остановки?)"; exit 1; }
echo "ALL PASS"
exit 0
