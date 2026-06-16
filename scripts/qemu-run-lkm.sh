# qemu-run-lkm.sh — компилировать LKM и прогнать тест в QEMU
#
# Использование: qemu-run-lkm.sh <module.c> <test_script.sh> [qemu_dir]
#
# Выход:
#   0 — тест прошёл
#   1 — тест упал
#   2 — ошибка компиляции
#   3 — QEMU недоступен

set -euo pipefail

MODULE_C="${1:-}"
TEST_SH="${2:-}"
QEMU_DIR="${3:-$(dirname "$(dirname "$0")")/.qemu}"

if [ -z "$MODULE_C" ] || [ -z "$TEST_SH" ]; then
    echo "Использование: $0 <module.c> <test.sh> [qemu_dir]"
    exit 1
fi

if [ ! -f "$QEMU_DIR/bzImage" ] || [ ! -f "$QEMU_DIR/initrd.img" ]; then
    echo "[!] QEMU не настроен. Запусти: bash scripts/qemu-setup.sh"
    exit 3
fi

BUILD_DIR="$(mktemp -d cppcourse_lkm_XXXXXX)"
SHARE_DIR="$(mktemp -d cppcourse_share_XXXXXX)"
trap 'rm -rf "$BUILD_DIR" "$SHARE_DIR"' EXIT

echo "=== Сборка модуля ==="

# Копируем исходник и собираем
cp "$MODULE_C" "$BUILD_DIR/module.c"
KERNEL_VERSION="$(uname -r)"
KDIR="/lib/modules/$KERNEL_VERSION/build"

cat > "$BUILD_DIR/Makefile" << EOF
obj-m := module.o
EOF

if ! make -C "$KDIR" M="$BUILD_DIR" modules 2>&1; then
    echo "[FAIL] Ошибка компиляции"
    exit 2
fi

KO="$BUILD_DIR/module.ko"
if [ ! -f "$KO" ]; then
    echo "[FAIL] module.ko не создан"
    exit 2
fi

echo "[OK] module.ko собран ($(du -sh "$KO" | cut -f1))"

# ---------- Подготовка share-директории ----------
cp "$KO" "$SHARE_DIR/module.ko"
cp "$TEST_SH" "$SHARE_DIR/run_test.sh"
chmod +x "$SHARE_DIR/run_test.sh"

echo ""
echo "=== Запуск в QEMU ==="

QEMU_OUT="$(mktemp)"
trap 'rm -f "$QEMU_OUT"; rm -rf "$BUILD_DIR" "$SHARE_DIR"' EXIT

timeout 45 qemu-system-x86_64 \
    -kernel "$QEMU_DIR/bzImage" \
    -initrd "$QEMU_DIR/initrd.img" \
    -append "root=/dev/ram console=ttyS0 quiet" \
    -m 256M \
    -nographic \
    -serial file:"$QEMU_OUT" \
    -nodefaults \
    -no-reboot \
    -virtfs "local,path=${SHARE_DIR},mount_tag=share,security_model=none,id=share0" \
    2>/dev/null || true

echo ""
echo "=== Вывод QEMU ==="
cat "$QEMU_OUT"

# Найти код выхода теста в выводе
EXIT_CODE=1
if grep -q "__TEST_EXIT_CODE__:0" "$QEMU_OUT"; then
    EXIT_CODE=0
fi

echo ""
if [ $EXIT_CODE -eq 0 ]; then
    echo "[PASS] Тест прошёл"
else
    echo "[FAIL] Тест не прошёл"
fi

exit $EXIT_CODE
