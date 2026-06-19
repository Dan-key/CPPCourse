#!/bin/sh
# Тест /dev/cppcfg: RCU-конфигурация (внутри QEMU). Успех = exit 0.

echo "=== K2 04-rcu-config ==="

if ! insmod /mnt/share/cppmod.ko; then
    echo "[FAIL] insmod не прошёл"; dmesg | tail -5; exit 1
fi
[ -c /dev/cppcfg ] || sleep 1
if [ ! -c /dev/cppcfg ]; then
    echo "[FAIL] /dev/cppcfg не создан"; dmesg | tail -5; exit 1
fi

check() {
    got="$(cat /dev/cppcfg)"
    if [ "$got" != "$1" ]; then
        echo "[FAIL] read='$got', ждали '$1'"; rmmod cppmod 2>/dev/null; exit 1
    fi
    echo "[OK] $got"
}

check "boot 0"                              # дефолт из module_init
echo "tuned 42" > /dev/cppcfg               # публикация новой версии (RCU)
check "tuned 42"
echo "fast 99" > /dev/cppcfg                # ещё одна публикация → старая освобождается
check "fast 99"

# Несколько быстрых публикаций подряд — проверка, что synchronize_rcu/kfree не падают.
i=0
while [ "$i" -lt 10 ]; do echo "v $i" > /dev/cppcfg; i=$((i+1)); done
check "v 9"

if dmesg | grep -qiE 'BUG:|Oops|WARNING:|rcu|use-after-free|slab'; then
    # rcu-стэллы, kfree старой версии раньше времени, и т.п.
    echo "[FAIL] ядро ругается:"; dmesg | grep -iE 'BUG:|Oops|WARNING:|rcu' | tail -8
    rmmod cppmod 2>/dev/null; exit 1
fi

rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod не прошёл"; exit 1; }
echo "ALL PASS"
exit 0
