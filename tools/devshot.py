#!/usr/bin/env python3
"""Drive WAVE OS over USB and grab screenshots.
usage: devshot.py PORT OUTDIR  "cmd cmd ..."   where cmd = K<x> | KENT | KESC | ... | shot:NAME | wait:SEC
"""
import serial, sys, time, os
from PIL import Image
port, out, script = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(out, exist_ok=True)
s = serial.Serial(); s.port = port; s.baudrate = 115200; s.timeout = 4; s.dtr = False; s.rts = False
s.open(); s.dtr = True
def wait_boot():
    t = time.time(); buf = b''
    while time.time() - t < 10:
        buf += s.read(200)
        if b'app=HOME' in buf: return
    print("boot msg not seen, continuing")
if os.environ.get("NOBOOT") is None: wait_boot()
time.sleep(0.5)
for c in script.split():
    if c.startswith('wait:'): time.sleep(float(c[5:])); continue
    if c.startswith('shot:'):
        data = None
        for attempt in range(4):
            s.reset_input_buffer(); s.write(b'SHOT\n' if c != 'shot:logo' else b'LOGO\n')
            t = time.time(); ok = False
            while time.time() - t < 3:
                l = s.readline()
                if l.startswith(b'SHOT'): ok = True; break
            if not ok: continue
            data = b''
            while len(data) < 240*135*2:
                d = s.read(240*135*2 - len(data))
                if not d: break
                data += d
            if len(data) == 240*135*2: break
            data = None
        if data is None: raise SystemExit("no shot for " + c)
        im = Image.new('RGB', (240, 135))
        px = im.load()
        for i in range(240*135):
            v = data[2*i] << 8 | data[2*i+1]
            px[i % 240, i // 240] = (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
        im = im.resize((720, 405), Image.NEAREST)
        im.save(os.path.join(out, c[5:] + '.png')); print("saved", c[5:]); continue
    s.write((c + '\n').encode()); time.sleep(0.35)
