#!/usr/bin/env python3
"""passtime.py <iolog.txt> [t_from_s t_to_s]: GPU time per pass, split into
vertex/tiler (vtc) and fragment, from kbase atom completion events.

Each atom's completion time is the first completion event for its atom
number after its submit. Start = max(submit time, completion of everything
the GPU ran before it) -- the driver under test runs one job chain at a time
per slot and the system driver orders vtc after the previous frag, so
busy time per atom ~= its completion minus the previous completion.
Event read times are batched by the reader, so single values are noisy;
the sums over seconds are what matter.
"""
import sys
from collections import defaultdict

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from blobdeps import parse  # noqa: E402


def main():
    subs, events = parse(sys.argv[1])
    t0 = subs[0]['t']
    lo = float(sys.argv[2]) if len(sys.argv) > 2 else 0
    hi = float(sys.argv[3]) if len(sys.argv) > 3 else 1e9

    ev_by_atom = defaultdict(list)
    for t, code, atom in events:
        ev_by_atom[atom].append(t)
    for v in ev_by_atom.values():
        v.sort()

    done = []          # (completion time, kind)
    for s in subs:
        if not lo <= (s['t'] - t0) / 1e9 <= hi:
            continue
        for a in s['atoms']:
            if a['k'] not in ('vtc', 'frag', 'compute'):
                continue
            ts = ev_by_atom.get(a['n'], [])
            t = next((x for x in ts if x >= s['t']), None)
            if t is not None:
                done.append((t, a['k'], s['t']))
    done.sort()
    busy = defaultdict(int)
    count = defaultdict(int)
    prev = None
    for t, k, ts in done:
        start = ts if prev is None else max(ts, prev)
        busy[k] += t - start
        count[k] += 1
        prev = t
    span = (done[-1][0] - done[0][0]) / 1e9 if len(done) > 1 else 1
    print(f'window {span:.1f} s, completions {len(done)}')
    for k in ('vtc', 'frag', 'compute'):
        if count[k]:
            print(f'{k:8s} {count[k]/span:6.1f}/s  avg {busy[k]/count[k]/1e6:7.2f} ms'
                  f'  busy {100*busy[k]/1e9/span:5.1f}% of wall time')


if __name__ == '__main__':
    main()
