#!/bin/bash
# ab10.sh <outdir> <rounds> <secs> <dxvk> -- scenes...   interleaved per scene
W=/data/data/com.termux/files/home/wltest; O=$1; R=$2; SEC=$3; V=$4; shift 5; mkdir -p $O
for r in $(seq 1 $R); do for sc in "$@"; do
  for cfg in drv17 drv18 drv18sync; do
    case $cfg in drv17) d=drv17; e="";; drv18) d=drv18; e="PANVK_KBASE_PROF=1";; drv18sync) d=drv18; e="PANVK_KBASE_SYNC_SUBMIT=1 PANVK_KBASE_PROF=1";; esac
    EXTRA="$e" bash /tmp/aiobench.sh $W/$d $O/r${r}_$cfg $V $SEC $sc
    echo "r$r $cfg $(tail -1 $O/r${r}_$cfg/summary.txt)" >> $O/all.txt
  done
done; done
touch $O/done
