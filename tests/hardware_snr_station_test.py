"""Realistic station SNR test: tune to an SSB station, capture IQ, and measure the
average signal-to-noise ratio over time.

For USB the transmitted audio sits above the suppressed carrier; the opposite
sideband is a same-bandwidth, same-conditions noise reference. SNR per short
frame = power(signal band) / power(opposite sideband), then averaged over the
capture (including natural speech pauses, plus a speech-active subset).

Run against different firmware setups (DC blocker / IQ balance / rate / gain) to
see which gives the best average SNR.

Usage:
  AIRSPY_TEST_FREQ=5505000 AIRSPY_TEST_STAGE=6 AIRSPY_TEST_GAIN=0 \
  AIRSPY_TEST_SECONDS=30 python3 tests/hardware_snr_station_test.py
"""
import os, socket, struct, time
import numpy as np

host = os.environ['AIRSPY_HOST']
FREQ = int(os.environ.get('AIRSPY_TEST_FREQ', '5505000'))
STAGE = int(os.environ.get('AIRSPY_TEST_STAGE', '6'))
GAIN = int(os.environ.get('AIRSPY_TEST_GAIN', '0'))  # 0 = receiver AGC
SECONDS = float(os.environ.get('AIRSPY_TEST_SECONDS', '30'))
RATE = 768000 >> STAGE
SIG_LO, SIG_HI = 300.0, 2700.0     # USB speech passband
REF_LO, REF_HI = -2700.0, -300.0   # opposite (lower) sideband noise reference
FRAME = 2048

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

send(0, struct.pack('<I', 0x020006a4) + b'snr-station')
msg(); msg()
setting(0, 1); setting(100, 2); setting(102, STAGE); setting(103, 0)
setting(2, GAIN); setting(101, FREQ); setting(1, 1)

raw = bytearray(); deadline = time.monotonic() + SECONDS
while time.monotonic() < deadline:
    h, b = msg()
    if (h[1] & 0xffff) == 101 and h[2] == 1: raw += b
setting(1, 0); s.close()

data = np.frombuffer(raw, dtype='<i2').astype(np.float64).reshape(-1, 2)
iq = data[:, 1] + 1j * data[:, 0]  # I,Q (wire is Q,I)
n = len(iq) // FRAME
iq = iq[:n * FRAME].reshape(n, FRAME)
win = np.hanning(FRAME)
spec = np.abs(np.fft.fft(iq * win, axis=1)) ** 2
freqs = np.fft.fftfreq(FRAME, 1 / RATE)

def band(lo, hi):
    m = (freqs >= lo) & (freqs <= hi)
    return m

msig = band(SIG_LO, SIG_HI); mref = band(REF_LO, REF_HI)
psig = spec[:, msig].sum(axis=1)
pref = spec[:, mref].sum(axis=1)
snr = 10 * np.log10((psig + 1e-12) / (pref + 1e-12))

print(f'freq {FREQ} stage {STAGE} ({RATE} Hz) gain {GAIN} frames {n} ({n*FRAME/RATE:.1f}s)')
print(f'avg SNR (all frames):    {np.mean(snr):6.2f} dB')
print(f'median SNR:              {np.median(snr):6.2f} dB')
# speech-active subset: frames in the top quartile of signal power
thr = np.percentile(psig, 75)
act = psig >= thr
print(f'avg SNR (speech-active): {np.mean(snr[act]):6.2f} dB  ({act.sum()} frames)')
print(f'signal power spread: p10 {10*np.log10(np.percentile(psig,10)+1e-12):.1f} dB, '
      f'p90 {10*np.log10(np.percentile(psig,90)+1e-12):.1f} dB')

# averaged spectrum around the band for a quick visual check
avg = spec.mean(axis=0)
def lvl(lo, hi):
    return 10 * np.log10(avg[band(lo, hi)].mean() + 1e-12)
print(f'signal band avg {lvl(SIG_LO,SIG_HI):.1f} dB, '
      f'ref band avg {lvl(REF_LO,REF_HI):.1f} dB, '
      f'overall {10*np.log10(avg.mean()+1e-12):.1f} dB')
# peak location in +/-6 kHz to confirm the signal is where expected
fk = freqs[np.argmax(avg)]
print(f'strongest bin in capture: {fk:+.0f} Hz')
