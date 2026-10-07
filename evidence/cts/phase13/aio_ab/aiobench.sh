#!/bin/bash
# aiobench.sh <drvdir> <outdir> <dxvk> <secs> <scene...>   (env: EXTRA="K=V ...")
# Runs AIO-64.exe --cube dx11 --scene <s> --bench <secs> uncapped and stores
# the bench CSV + summary line per scene. Scene "draws:N" = drawstress --draws N.
H=/data/data/com.termux/files/home; W=$H/wltest; S=$H/panvk-g57/phase4/src
P=/data/data/com.termux/files/usr; R=$H/hangover/root$P
DRV=$1; O=$2; V=$3; SEC=$4; shift 4; mkdir -p $O
RD="$W/AIO Results/Benchmark"
for sc in "$@"; do
  case $sc in draws:*) args="--cube dx11 --scene drawstress --draws ${sc#draws:}";; vk) args="--cube vk";; *) args="--cube dx11 --scene $sc";; esac
  t=$O/${sc/:/_}; mkdir -p $t; rm -f "$RD"/AIO-Graphics-Test_bench*.txt "$RD"/AIO-Graphics-Test_bench.csv
  cd $W
  pkill -x feh 2>/dev/null
  EXE_ARGS="$args --bench $SEC --autoclose 2" DXVK_LOG_PATH=$t timeout $((SEC+90)) $S/run_dxvk.sh $V $DRV $W/AIO-64.exe MESA_SHADER_CACHE_DISABLE=true DXVK_HUD=${HUD:-devinfo,fps,frametimes,gpuload,submissions} $EXTRA > $t/out.log 2>&1
  rc=$?
  env WINEPREFIX=$W/wpfx LD_LIBRARY_PATH=$R/lib $R/opt/hangover-wine/bin/wineserver -k 2>/dev/null; sleep 2
  cp "$RD"/AIO-Graphics-Test_bench.csv $t/bench.csv 2>/dev/null
  hdr=$(grep -a '^# avg_fps' $t/bench.csv 2>/dev/null | head -1)
  echo "$sc rc=$rc $hdr" >> $O/summary.txt
done
pgrep -x feh >/dev/null || setsid -f env DISPLAY=:0 feh -F -R 1 /tmp/live/dashboard.png > /tmp/feh.log 2>&1 < /dev/null
touch $O/done
