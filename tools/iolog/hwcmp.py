#!/usr/bin/env python3
"""hwcmp.py <a.bin> <b.bin> [fps_a fps_b] [--all]: compare two hwcnt.c
captures (per second; per frame too when the frame rates are given).

Without --all only named counters (G77 layout, see hwcnt.py) are shown.
"""
import sys
from collections import defaultdict

sys.path.insert(0, __file__.rsplit('/', 1)[0])
import hwcnt  # noqa: E402


def agg(path):
    s = hwcnt.read(path)
    dur = sum(x[1] - x[0] for x in s) / 1e9
    tot = defaultdict(int)
    for x in s:
        for t, idx, st, vals in x[4]:
            for i, v in enumerate(vals):
                if i >= 4:
                    tot[(t, i)] += v
    return dur, tot


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    show_all = '--all' in sys.argv
    da, a = agg(args[0])
    db, b = agg(args[1])
    fa = float(args[2]) if len(args) > 3 else None
    fb = float(args[3]) if len(args) > 3 else None
    nm = hwcnt.names()
    print(f'A {args[0]}: {da:.1f} s   B {args[1]}: {db:.1f} s')
    hdr = f"{'counter':40s} {'A M/s':>9s} {'B M/s':>9s} {'B/A':>6s}"
    if fa:
        hdr += f" {'A /frame':>11s} {'B /frame':>11s} {'B/A':>6s}"
    print(hdr)
    for k in sorted(set(a) | set(b)):
        n = nm.get(k)
        if n is None and not show_all:
            continue
        n = n or f'{hwcnt.SHORT[k[0]]}_{k[1]}'
        va, vb = a.get(k, 0) / da, b.get(k, 0) / db
        if max(va, vb) < 5e4:
            continue
        r = vb / va if va else float('inf')
        line = f'{hwcnt.SHORT[k[0]]} {n:37s} {va/1e6:9.2f} {vb/1e6:9.2f} {r:6.2f}'
        if fa:
            pa, pb = va / fa, vb / fb
            rf = pb / pa if pa else float('inf')
            line += f' {pa/1e6:9.3f} M {pb/1e6:9.3f} M {rf:6.2f}'
        print(line)


if __name__ == '__main__':
    main()
