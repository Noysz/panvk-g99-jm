#!/bin/bash
W=/data/data/com.termux/files/home/wltest; O=$1; R=$2; SEC=$3; V=$4; shift 5; mkdir -p $O
for r in $(seq 1 $R); do for sc in "$@"; do for p in pkg_v007 pkg_v008 pkg_v009; do
  bash /tmp/aiobench.sh $W/$p $O/r${r}_$p $V $SEC $sc
  echo "r$r $p $(tail -1 $O/r${r}_$p/summary.txt)" >> $O/all.txt
done; done; done
touch $O/done
