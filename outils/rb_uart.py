#!/usr/bin/env python3
# Serial 9600 8N1 decoder (idle high) for sigrok captures of LNK2 ↔ ESP-ME3 (found on 01/11/2026:
# once its session is open the ESP-ME3 talks UART, not Manchester). Usage: rb_uart.py files.sr
import sys, glob
sys.path.insert(0, ".")
from rb_decode import lire_sr, paliers
BIT = 1e6 / 9600
def octets(p):
    # sequence of levels sampled at the centre of each bit, starting from each start bit
    ech = []
    for n, du, t in p:
        ech.append((t, t + du, n))
    def niveau(t):
        lo, hi = 0, len(ech) - 1
        while lo < hi:
            m = (lo + hi) // 2
            if ech[m][1] <= t: lo = m + 1
            else: hi = m
        return ech[lo][2]
    out = []
    i = 0
    fin = ech[-1][1]
    for (t0, t1, n) in ech:
        if n == 0 and (not out or t0 >= out[-1][0] + 9.5 * BIT):
            if t0 + 10 * BIT > fin: break
            v = 0
            for b in range(8):
                v |= niveau(t0 + (1.5 + b) * BIT) << b
            stop = niveau(t0 + 9.5 * BIT)
            out.append((t0, v, stop))
    return out
for f in sorted(sys.argv[1:]):
    freq, d, bits = lire_sr(f)
    evts = []
    for voie, nom in ((0, "LNK2→prog"), (1, "prog→LNK2")):
        for t, v, s in octets(paliers(d, bits[voie], freq)):
            evts.append((t, nom, v, s))
    evts.sort()
    ligne, dern, nomc = [], -1e9, None
    def flush():
        if ligne: print(f"{f[-14:-3]} {ligne[0][0]/1000:9.1f} ms {nomc}: " + " ".join(f"{v:02X}" + ("" if s else "!") for _, v, s in ligne))
    for e in evts:
        if e[1] != nomc or e[0] - dern > 3 * BIT * 10:
            flush(); ligne = []; nomc = e[1]
        ligne.append((e[0], e[2], e[3])); dern = e[0]
    flush()
