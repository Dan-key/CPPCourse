#!/bin/bash
# qemu-run-lkm.sh — компилировать LKM и прогнать тест в QEMU
#
# Использование: qemu-run-lkm.sh <module.c> <test_script.sh> [qemu_dir]
#
# Доставка module.ko и теста в гостя — через ДОПИСАННЫЙ initramfs (cpio-конкатенация),
# а не 9p: 9p в дистро-ядре собран модулем (=m) и в минимальном initramfs недоступен,
# тогда как initramfs (CONFIG_BLK_DEV_INITRD=y) и devtmpfs работают на любом ядре.
#
# Выход:
#   0 — тест прошёл
#   1 — тест упал
#   2 — ошибка компиляции
#   3 — QEMU недоступен

set -euo pipefail

MODULE_C="${1:-}"
TEST_SH="${2:-}"
QEMU_DIR="${3:-$(cd "$(dirname "$(dirname "$0")")" && pwd)/.qemu}"

if [ -z "$MODULE_C" ] || [ -z "$TEST_SH" ]; then
    echo "Использование: $0 <module.c> <test.sh> [qemu_dir]"
    exit 1
fi

if [ ! -f "$QEMU_DIR/bzImage" ] || [ ! -f "$QEMU_DIR/initrd.img" ]; then
    echo "[!] QEMU не настроен. Запусти: bash scripts/qemu-setup.sh"
    exit 3
fi

# Абсолютные временные каталоги (kbuild требует абсолютный M=, иначе «directory does not exist»).
BUILD_DIR="$(mktemp -d)"
EXTRA_DIR="$(mktemp -d)"
QEMU_OUT="$(mktemp)"
EXTRA_CPIO="$(mktemp).cpio.gz"
COMBINED_IMG="$(mktemp).img"
trap 'rm -rf "$BUILD_DIR" "$EXTRA_DIR" "$QEMU_OUT" "$EXTRA_CPIO" "$COMBINED_IMG"' EXIT

echo "=== Сборка модуля ==="

# Имя модуля — cppmod (НЕ "module": generic-имя даёт "module is already loaded"/EINVAL).
# В тесте: insmod /mnt/share/cppmod.ko ; rmmod cppmod
cp "$MODULE_C" "$BUILD_DIR/cppmod.c"
KERNEL_VERSION="$(uname -r)"
KDIR="/lib/modules/$KERNEL_VERSION/build"

cat > "$BUILD_DIR/Makefile" << 'EOF'
obj-m := cppmod.o
EOF

# M= — АБСОЛЮТНЫЙ путь (BUILD_DIR от mktemp -d), иначе make -C KDIR не найдёт каталог.
if ! make -C "$KDIR" M="$BUILD_DIR" modules 2>&1; then
    echo "[FAIL] Ошибка компиляции"
    exit 2
fi

KO="$BUILD_DIR/cppmod.ko"
if [ ! -f "$KO" ]; then
    echo "[FAIL] module.ko не создан"
    exit 2
fi

echo "[OK] module.ko собран ($(du -sh "$KO" | cut -f1))"

# ---------- Доп. initramfs: /init + /mnt/share/{module.ko,run_test.sh} ----------
# Дописывается к базовому initrd; ядро распаковывает оба cpio по порядку (поздние
# файлы перекрывают ранние). /init из этого cpio — точка входа initramfs.
mkdir -p "$EXTRA_DIR/mnt/share"
cp "$KO" "$EXTRA_DIR/mnt/share/cppmod.ko"
cp "$TEST_SH" "$EXTRA_DIR/mnt/share/run_test.sh"
chmod +x "$EXTRA_DIR/mnt/share/run_test.sh"

cat > "$EXTRA_DIR/init" << 'INIT_EOF'
#!/bin/busybox sh
# Точка входа initramfs: поднять окружение busybox и прогнать тест.
/bin/busybox --install -s /bin 2>/dev/null
mount -t proc none /proc 2>/dev/null
mount -t sysfs none /sys 2>/dev/null
mount -t devtmpfs devtmpfs /dev 2>/dev/null
export PATH=/bin:/sbin
if [ -f /mnt/share/run_test.sh ]; then
    /bin/busybox sh /mnt/share/run_test.sh
    echo "__TEST_EXIT_CODE__:$?"
else
    echo "__TEST_EXIT_CODE__:127"
fi
/bin/busybox sync 2>/dev/null
poweroff -f 2>/dev/null || { echo o > /proc/sysrq-trigger; }
INIT_EOF
chmod +x "$EXTRA_DIR/init"

( cd "$EXTRA_DIR" && find . | cpio -o -H newc 2>/dev/null ) | gzip > "$EXTRA_CPIO"

# Конкатенация base + extra в ОДИН initrd-файл: ядро распаковывает несколько
# сжатых cpio подряд (как ранний микрокод-initramfs). Запятую в -initrd QEMU 10.2
# трактует как одно имя файла, поэтому склеиваем сами.
cat "$QEMU_DIR/initrd.img" "$EXTRA_CPIO" > "$COMBINED_IMG"

echo ""
echo "=== Запуск в QEMU ==="

# Ядро само запускает /init из initramfs (root=/9p не нужны).
# console=ttyS0 → -serial file (НЕ -nographic: оно увело бы ttyS0 на stdio,
# а -serial file: получил бы пустой ttyS1).
timeout 45 qemu-system-x86_64 \
    -kernel "$QEMU_DIR/bzImage" \
    -initrd "$COMBINED_IMG" \
    -append "console=ttyS0 quiet" \
    -m 256M \
    -display none \
    -monitor none \
    -serial file:"$QEMU_OUT" \
    -no-reboot \
    2>/dev/null || true

echo ""
echo "=== Вывод QEMU ==="
cat "$QEMU_OUT"

EXIT_CODE=1
if grep -q "__TEST_EXIT_CODE__:0" "$QEMU_OUT"; then
    EXIT_CODE=0
fi

echo ""
if [ "$EXIT_CODE" -eq 0 ]; then
    echo "[PASS] Тест прошёл"
else
    echo "[FAIL] Тест не прошёл"
fi

exit "$EXIT_CODE"
