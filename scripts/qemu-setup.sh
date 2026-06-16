#!/bin/bash
# qemu-setup.sh — одноразовая настройка QEMU-среды для тестирования kernel-модулей
#
# Что делает:
#   1. Устанавливает зависимости (QEMU, gcc, kernel-headers, busybox)
#   2. Создаёт минимальный initrd с busybox
#   3. Копирует bzImage хост-ядра
#   4. Создаёт вспомогательные скрипты
#
# После запуска: .qemu/ содержит всё необходимое для qemu-run-lkm.sh
#
# Требования: Ubuntu/Debian, root или sudo

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
QEMU_DIR="$REPO_DIR/.qemu"
KERNEL_VERSION="$(uname -r)"

echo "=== QEMU Setup для трека системного программиста ==="
echo "Ядро хоста: $KERNEL_VERSION"
echo "Целевая папка: $QEMU_DIR"

# ---------- 1. Проверка зависимостей ----------
check_dep() {
    if ! command -v "$1" &>/dev/null; then
        echo "[!] $1 не найден. Устанавливаем..."
        sudo apt-get install -y "$2" 2>/dev/null || {
            echo "    Ошибка: установи вручную: apt install $2"
            exit 1
        }
    else
        echo "[OK] $1"
    fi
}

echo ""
echo "--- Проверка зависимостей ---"
check_dep qemu-system-x86_64 "qemu-system-x86"
check_dep gcc                 "build-essential"
check_dep busybox             "busybox-static"

# Заголовки для версии хост-ядра
if [ ! -d "/lib/modules/$KERNEL_VERSION/build" ]; then
    echo "[!] Заголовки ядра не найдены. Устанавливаем linux-headers-$KERNEL_VERSION..."
    sudo apt-get install -y "linux-headers-$KERNEL_VERSION" || {
        echo "    Попробуй: apt install linux-headers-generic"
        exit 1
    }
fi
echo "[OK] kernel headers ($KERNEL_VERSION)"

mkdir -p "$QEMU_DIR"

# ---------- 2. bzImage хост-ядра ----------
echo ""
echo "--- Копируем bzImage ---"
BZIMAGE="/boot/vmlinuz-$KERNEL_VERSION"
if [ ! -f "$BZIMAGE" ]; then
    # Попробуем найти любой vmlinuz
    BZIMAGE="$(ls /boot/vmlinuz-* 2>/dev/null | head -1)"
    if [ -z "$BZIMAGE" ]; then
        echo "[!] bzImage не найден в /boot/"
        exit 1
    fi
fi
cp "$BZIMAGE" "$QEMU_DIR/bzImage"
echo "[OK] bzImage → $QEMU_DIR/bzImage"

# ---------- 3. Initrd с busybox ----------
echo ""
echo "--- Создаём initrd ---"

INITRD_DIR="$(mktemp -d)"
trap 'rm -rf "$INITRD_DIR"' EXIT

mkdir -p "$INITRD_DIR"/{bin,sbin,dev,proc,sys,tmp,mnt/share,lib/modules}

# busybox
BUSYBOX_BIN="$(which busybox)"
cp "$BUSYBOX_BIN" "$INITRD_DIR/bin/busybox"
chmod +x "$INITRD_DIR/bin/busybox"

# Создать symlinks для основных утилит
for cmd in sh ls cat echo mkdir insmod rmmod dmesg modprobe lsmod grep; do
    ln -sf /bin/busybox "$INITRD_DIR/bin/$cmd"
done

# /sbin/init — точка входа
cat > "$INITRD_DIR/sbin/init" << 'INIT_EOF'
#!/bin/sh
mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null || mknod /dev/null c 1 3
# Монтировать 9p-share (файлы теста)
mount -t 9p -o trans=virtio,version=9p2000.L share /mnt/share 2>/dev/null && {
    # Запустить тестовый скрипт если он есть
    if [ -f /mnt/share/run_test.sh ]; then
        export PATH=/bin:/sbin
        sh /mnt/share/run_test.sh
        echo "__TEST_EXIT_CODE__:$?"
    else
        echo "__TEST_EXIT_CODE__:127"
    fi
} || {
    echo "[!] 9p mount failed"
    echo "__TEST_EXIT_CODE__:1"
}
poweroff -f 2>/dev/null || echo o > /proc/sysrq-trigger
INIT_EOF
chmod +x "$INITRD_DIR/sbin/init"

# Создать initrd
(cd "$INITRD_DIR" && find . | cpio -o -H newc 2>/dev/null) | gzip > "$QEMU_DIR/initrd.img"
echo "[OK] initrd.img создан"

# ---------- 4. Вспомогательный скрипт сборки модуля ----------
cat > "$QEMU_DIR/build-module.sh" << 'BUILD_EOF'
#!/bin/bash
# Сборка kernel-модуля против хост-ядра
# Использование: build-module.sh <module.c> <output-dir>
set -euo pipefail
MODULE_C="$1"
OUT_DIR="$2"
KERNEL_VERSION="$(uname -r)"
KDIR="/lib/modules/$KERNEL_VERSION/build"

cp "$MODULE_C" "$OUT_DIR/module.c"
cat > "$OUT_DIR/Makefile" << EOF
obj-m := module.o
EOF
make -C "$KDIR" M="$OUT_DIR" modules 2>&1
BUILD_EOF
chmod +x "$QEMU_DIR/build-module.sh"

echo ""
echo "=== Готово! ==="
echo ""
echo "Для тестирования LKM запускай через сервер или:"
echo "  bash scripts/qemu-run-lkm.sh <module.c> <test.sh>"
echo ""
echo "Содержимое $QEMU_DIR:"
ls -lh "$QEMU_DIR/"
