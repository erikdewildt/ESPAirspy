"""Offline IQ-imbalance analysis on captured raw IQ: estimate g/sin(phi) with the
same statistics the firmware uses, apply the Gram-Schmidt correction, and
measure image rejection before/after to determine whether the correction helps
or hurts on real RF."""
import os, socket, struct, time
import numpy as np

host = os.environ['AIRSPY_HOST']
FREQ = int(os.environ.get('AIRSPY_TEST_FREQ', '7074000'))
NFFT = 32768
SECONDS = float(os.environ.get('AIRSPY_TEST_SECONDS', '4'))

s = socket.create_connection((host, 5555), timeout=5)
def send(c, b=b''): s.sendall(struct.pack('<II', c, len(b)) + b)
def setting(k, v): send(2, struct.pack('<II', k, v))
def exact(n):
    out = bytearray(n); view = memoryview(out)
    while view:
        g = s.recv_into(view)
        if not g: raise EOFError()
        view = view[g:]
    return out
def msg():
    h = struct.unpack('<IIIII', exact(20)); return h, exact(h[4])

send(0, struct.pack('<I', 0x020006a4) + b'offline-balance')
msg(); msg()
setting(0, 1); setting(100, 2); setting(102, 0); setting(103, 0)
setting(101, FREQ); setting(1, 1)

raw = bytearray(); deadline = time.monotonic() + SECONDS
while time.monotonic() < deadline:
    h, b = msg()
    if (h[1] & 0xffff) == 101 and h[2] == 1: raw += b
setting(1, 0); s.close()

data = np.frombuffer(raw, dtype='<i2').astype(np.float64).reshape(-1, 2)
# Airspy wire order is Q,I -> complex = I + jQ
I = data[:, 1].astype(np.float64)
Q = data[:, 0].astype(np.float64)

# --- estimate imbalance (same as firmware) ---
Eii = np.mean(I * I); Eqq = np.mean(Q * Q); Eiq = np.mean(I * Q)
g = np.sqrt(Eqq / Eii)
sinphi = Eiq / np.sqrt(Eii * Eqq)
cosphi = np.sqrt(1 - sinphi * sinphi)
print(f'Estimated: g = {g:.4f}, sin(phi) = {sinphi:.4f} (phi ~ {np.degrees(np.arcsin(np.clip(sinphi,-1,1))):.2f} deg)')
print(f'E[I^2]={Eii:.0f}  E[Q^2]={Eqq:.0f}  E[I*Q]={Eiq:.0f}')

def spectrum(iq):
    n = len(iq) // NFFT * NFFT
    x = iq[:n].reshape(-1, NFFT)
    p = np.mean(np.abs(np.fft.fft(x * np.hanning(NFFT), axis=1)) ** 2, axis=0)
    return np.fft.fftshift(p), np.fft.fftshift(np.fft.fftfreq(NFFT, 1 / 768000))

def irr(iq):
    p, f = spectrum(iq)
    db = 10 * np.log10(p + 1e-12)
    # strongest peaks (exclude near DC), find image rejection
    mask = np.abs(f) > 20000
    order = np.argsort(p[mask])[::-1]
    seen = set(); out = []
    for idx in order:
        freq = f[mask][idx]
        if any(abs(freq - sf) < 3000 for sf in seen): continue
        im = np.argmin(np.abs(f + freq))
        out.append((freq, db[mask][idx], db[im]))
        seen.add(freq)
        if len(seen) >= 6: break
    return out

def report(tag, iq):
    rows = irr(iq)
    print(f'\n{tag} (image rejection per peak):')
    for freq, lvl, imlvl in rows:
        print(f'  {freq:+.0f} Hz: peak {lvl:.1f} dB, image {imlvl:.1f} dB, rejection {lvl-imlvl:.1f} dB')

orig = I + 1j * Q
report('BEFORE correction', orig)

# --- apply firmware's correction: I'=I, Q'=(Q - I*sinphi)/(g*cosphi) ---
inv = 1.0 / (g * cosphi)
a = sinphi * inv; b = inv
Qc = Q * b - I * a
corr = I + 1j * Qc
report('AFTER firmware correction', corr)

# --- also try the exact inverse: Q' = Q/(g cosphi) - I*tan(phi) ---
Qc2 = Q / (g * cosphi) - I * np.tan(np.arcsin(sinphi))
corr2 = I + 1j * Qc2
report('AFTER exact inverse', corr2)
