#!/bin/sh
# Тест /dev/cppseq: seqlock (внутри QEMU). Успех = exit 0.

echo "=== K2 06-seqlock-pair ==="

if ! insmod /mnt/share/cppmod.ko; then
    echo "[FAIL] insmod не прошёл"; dmesg | tail -5; exit 1
fi
[ -c /dev/cppseq ] || sleep 1
if [ ! -c /dev/cppseq ]; then
    echo "[FAIL] /dev/cppseq не создан"; dmesg | tail -5; exit 1
fi

check() {
    got="$(cat /dev/cppseq)"
    if [ "$got" != "$1" ]; then
        echo "[FAIL] read='$got', ждали '$1'"; rmmod cppmod 2>/dev/null; exit 1
    fi
    echo "[OK] $got"
}

check "0 0"
echo 7  > /dev/cppseq; check "7 7"          # a == b
echo 42 > /dev/cppseq; check "42 42"
echo -5 > /dev/cppseq; check "-5 -5"

# Стресс: писатель в фоне молотит обновления, читатель должен ВСЕГДА видеть a==b.
( i=0; while [ "$i" -lt 2000 ]; do echo "$i" > /dev/cppseq; i=$((i+1)); done ) &
WPID=$!
torn=0
j=0
while [ "$j" -lt 300 ]; do
    snap="$(cat /dev/cppseq)"
    A="${snap% *}"; B="${snap#* }"
    [ "$A" = "$B" ] || { torn=1; echo "[FAIL] рваный снимок: '$snap'"; break; }
    j=$((j+1))
done
wait "$WPID"
[ "$torn" -eq 0 ] && echo "[OK] читатель НИКОГДА не видел a != b при конкурентной записи"
[ "$torn" -eq 0 ] || { rmmod cppmod 2>/dev/null; exit 1; }

if dmesg | grep -qiE 'BUG:|Oops|WARNING:'; then
    echo "[FAIL] ядро ругается:"; dmesg | grep -iE 'BUG:|Oops|WARNING:' | tail -5
    rmmod cppmod 2>/dev/null; exit 1
fi

rmmod cppmod 2>/dev/null || { echo "[FAIL] rmmod не прошёл"; exit 1; }
echo "ALL PASS"
exit 0
