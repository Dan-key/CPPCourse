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
