#!/usr/bin/env python3
"""sdbstats.py <log>...: aggregate BIFROST_MESA_DEBUG=shaderdb lines per stage.

Prints, per stage: shader count, share at 1 thread (64 registers), average
instructions, cycles, load/store cycles, total spills, and how many shaders
are load/store-bound (ls cycles == total cycles).
"""
import re
import sys
from collections import defaultdict

PAT = re.compile(r'MESA_SHADER_(\w+) shader: (\d+) instrs, ([\d.]+) cycles, '
                 r'([\d.]+) fma, ([\d.]+) cvt, ([\d.]+) sfu, ([\d.]+) alu, '
                 r'([\d.]+) v, ([\d.]+) t, ([\d.]+) ls, .*?(\d+) threads, '
                 r'(\d+) loops, (\d+):(\d+) spills:fills')

for path in sys.argv[1:]:
    st = defaultdict(list)
    for line in open(path, errors='replace'):
        m = PAT.search(line)
        if m:
            st[m.group(1)].append(m.groups()[1:])
    print(f'## {path}')
    for stage, rows in sorted(st.items()):
        n = len(rows)
        avg = lambda i: sum(float(r[i]) for r in rows) / n
        t1 = sum(1 for r in rows if r[9] == '1')
        lsb = sum(1 for r in rows if float(r[8]) >= float(r[1]) > 0)
        print(f'{stage:9s} n={n:4d} 64reg {100*t1/n:3.0f}%  instrs {avg(0):6.1f}  '
              f'cycles {avg(1):6.2f}  fma {avg(2):5.2f}  ls {avg(8):6.2f}  '
              f'ls-bound {100*lsb/n:3.0f}%  spills {sum(int(r[11]) for r in rows)}')
