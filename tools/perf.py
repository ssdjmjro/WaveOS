#!/usr/bin/env python3
"""Drive WAVE OS over USB and print the [perf] heartbeat for each step.
usage: perf.py PORT "K7 KENT wait:11 KESC ..."  (wait lets the 5s heartbeat report)"""
import serial, sys, time
import os
ALL = os.environ.get('ALL') == '1'  # print every serial line
port, script = sys.argv[1], sys.argv[2]
s = serial.Serial(); s.port = port; s.baudrate = 115200; s.timeout = 0.2; s.dtr = False; s.rts = False
s.open(); s.dtr = True
buf = b''
def pump(sec):
    global buf
    t = time.time()
    while time.time() - t < sec:
        buf += s.read(500)
        while b'\n' in buf:
            l, buf = buf.split(b'\n', 1)
            if ALL or b'[perf]' in l or b'[bench]' in l or b'[gfx]' in l or b'[clip]' in l or b'rror' in l or b'abort' in l.lower(): print(l.decode(errors='replace').strip(), flush=True)
pump(8)
for c in script.split():
    if c.startswith('wait:'): pump(float(c[5:])); continue
    if c.startswith('#'): print('---', c[1:], flush=True); continue
    s.write((c + '\n').encode()); pump(0.35)
