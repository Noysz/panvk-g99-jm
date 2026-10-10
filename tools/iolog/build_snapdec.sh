#!/bin/bash
# Build snapdec against mesa-up/buildU static libraries, with the flags the
# Mesa build uses for genxml/decode_common.c (snapdec.flags, paths relative
# to buildU).
T=$(cd "$(dirname "$0")" && pwd)
B=/data/data/com.termux/files/home/panvk-g57/mesa-up/buildU
cd $B
LIBS=$(grep -m1 -A3 'build src/panfrost/vulkan/libvulkan_panfrost.so:' build.ninja |
       grep -oE 'src/[^ ]+\.a' | grep -v -e panfrost_icd -e /vulkan/ | sort -u)
eval clang -O1 -g $(cat $T/snapdec.flags) -I../src/panfrost/compiler -o $T/snapdec $T/snapdec.c \
   -Wl,--start-group $LIBS -Wl,--end-group -lz -lzstd -lm -ldl -lpthread -lexpat -lc++ 2>&1 |
   grep -E 'error|undefined' | head -20
ls -la $T/snapdec 2>/dev/null | awk '{print $5}'
