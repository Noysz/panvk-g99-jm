#!/data/data/com.termux/files/usr/bin/bash
# build.sh - compile shader + harness for the indirect-dispatch test.
#
# Requires: glslangValidator (pkg install vulkan-tools), cc/clang (Termux).
#
# Usage: ./build.sh
set -e
cd "$(dirname "$0")"

MESA_INCLUDE="${MESA_INCLUDE:-$HOME/funnymdzz-mesa/include}"

command -v glslangValidator >/dev/null 2>&1 || {
  echo "glslangValidator not found - try: pkg install vulkan-tools"; exit 1; }
command -v cc >/dev/null 2>&1 || { echo "cc not found"; exit 1; }

echo "=== compiling shaders/write_id.comp -> .spv ==="
glslangValidator -V shaders/write_id.comp -o shaders/write_id.spv

echo "=== compiling indirect_dispatch_test.c ==="
cc -O2 -o indirect_dispatch_test indirect_dispatch_test.c \
  -I "$MESA_INCLUDE" -lvulkan -ldl

echo "=== done ==="
echo "run with:"
echo "  PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1 ./indirect_dispatch_test"
