#!/bin/bash
# run_ge.sh <dxvk-version> <drvdir> <exe> [extra env...]
# Same stack as run_dxvk.sh (real DXVK, FEX arm64ec, Termux loader ->
# Winlator libvulkan_wrapper.so -> adrenotools -> our driver, Termux:X11 on
# DISPLAY=:0), but on GE-Proton 11.0-7.1 arm64ec, The412Banner's bionic build
# (winlator-contents release bionic-layers-20261007-v14).
#
# One-time setup (what was done for the 2026-10-10 runs):
#  - extract the .wcp (zstd tar) to $GE; extract its prefixPack.txz and move
#    .wine to $W/wpfx_ge; in dosdevices drop e: and point z: at /
#  - copy libarm64ecfex.dll and libwow64fex.dll (Hangover's FEX) into
#    $W/wpfx_ge/drive_c/windows/system32 (the prefix registry already names
#    them as the Wow64 emulators); run "wine wineboot -u" once
#  - build noshm.c: clang -O2 -shared -fPIC -o libnoshm.so noshm.c
#
# Needed on top of run_dxvk.sh:
#  - LD_PRELOAD libnoshm.so: GE's winex11 draws through MIT-SHM with
#    Winlator's libandroid-sysvshm segments, which Termux:X11 cannot attach,
#    so every window stays unmapped/black (see noshm.c)
#  - winmm=n,b: Tomb Raider's own winmm.dll provides its GDK runtime; with
#    Wine's builtin the game exits right after start
#  - a virtual desktop, like Winlator
H=/data/data/com.termux/files/home
P=/data/data/com.termux/files/usr
R=$H/hangover/root$P
O=${GE:-$H/panvk-refs/proton/ge-11.0-7.1-v14}
NOSHM=${NOSHM:-$H/panvk-refs/proton/noshm/libnoshm.so}
W=$H/wltest
F=$W/imagefs/usr/lib
V=${1:?dxvk version}; DRV=${2:?drvdir}; EXE=${3:?exe}; shift 3
D=/tmp/dxvk/$V/system32
[ -f "$D/d3d11.dll" ] || { echo "no DXVK $V in $D"; exit 2; }
[ -f "$NOSHM" ] || { echo "no $NOSHM (build noshm.c)"; exit 2; }
S32=$W/wpfx_ge/drive_c/windows/system32
for dll in d3d11 dxgi d3d9 d3d10core d3d8; do [ -f "$D/$dll.dll" ] && cp -f "$D/$dll.dll" "$S32/$dll.dll"; done
# ICD json pointing at the Winlator wrapper (absolute path)
cat > $W/wrapper_icd.json <<EOF
{ "file_format_version": "1.0.0",
  "ICD": { "library_path": "$F/libvulkan_wrapper.so", "api_version": "1.3.303" } }
EOF
exec env -u PAN_I_WANT_A_BROKEN_VULKAN_DRIVER \
  LD_PRELOAD=$NOSHM WINEPREFIX=$W/wpfx_ge WINEDEBUG=${WINEDEBUG:--all} DISPLAY=:0 \
  LD_LIBRARY_PATH=$R/lib:$F:$O/lib \
  VK_ICD_FILENAMES=$W/wrapper_icd.json \
  ADRENOTOOLS_DRIVER_PATH=$DRV/ ADRENOTOOLS_HOOKS_PATH=$F/ ADRENOTOOLS_DRIVER_NAME=libvulkan_panfrost.so \
  WINEDLLOVERRIDES="d3d11,dxgi,d3d9,d3d10core,d3d8=n;winmm=n,b" \
  DXVK_LOG_LEVEL=${DXVK_LOG_LEVEL:-info} DXVK_LOG_PATH=${DXVK_LOG_PATH:-$W} \
  "$@" $O/bin/wine explorer /desktop=shell,${GE_DESKTOP:-848x480} "$EXE" ${EXE_ARGS}
