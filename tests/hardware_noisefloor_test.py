"""Measure the receiver noise floor (median spectral level) as a function of
gain settings, to determine whether a "high noise floor" is receiver/AGC related
or environmental. For each setting it sets gain via the HTTP endpoint, captures
IQ, and reports the median noise floor plus the strongest signal.

Usage:
  AIRSPY_TEST_FREQ=5505000 python3 tests/hardware_noisefloor_test.py
"""
import os, socket, struct, time, urllib.request
import numpy as np

host = os.environ['AIRSPY_HOST']
FREQ = int(os.environ.get('AIRSPY_TEST_FREQ', '5505000'))
SECONDS = float(os.environ.get('AIRSPY_TEST_SECONDS', '3'))
NFFT = 32768
RATE = 768000

# (gain, lna, threshold, label): gain 0 = AGC; 1..9 = manual ATT (48..0 dB).
SETTINGS = [
    (0, 0, 0, 'AGC'),
    (9, 0, 0, 'ATT 0 dB  (gain 9)'),
    (7, 0, 0, 'ATT 12 dB (gain 7)'),
    (5, 0, 0, 'ATT 24 dB (gain 5)'),
    (3, 0, 0, 'ATT 36 dB (gain 3)'),
    (1, 0, 0, 'ATT 48 dB (gain 1)'),
]

def set_gain(gain, lna, threshold):
    url = f'http://{host}/api/airspy/gain?gain={gain}&lna={lna}&threshold={threshold}'
    req = urllib.request.Request(url, data=b'', method='POST')
    urllib.request.urlopen(req, timeout=5).read()
    time.sleep(0.4)

def capture():
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

    send(0, struct.pack('<I', 0x020006a4) + b'noisefloor')
    msg(); msg()
    setting(0, 1); setting(100, 2); setting(102, 0); setting(103, 0)  # 768k, 0 dB
    setting(101, FREQ); setting(1, 1)
    raw = bytearray(); deadline = time.monotonic() + SECONDS
    while time.monotonic() < deadline:
        h, b = msg()
        if (h[1] & 0xffff) == 101 and h[2] == 1: raw += b
    setting(1, 0); s.close()
    data = np.frombuffer(raw, dtype='<i2').astype(np.float64).reshape(-1, 2)
    return data[:, 1] + 1j * data[:, 0]  # I + jQ (wire is Q,I)

def noisefloor(iq):
    n = len(iq) // NFFT * NFFT
    x = iq[:n].reshape(-1, NFFT)
    p = np.mean(np.abs(np.fft.fft(x * np.hanning(NFFT), axis=1)) ** 2, axis=0)
    p = np.fft.fftshift(p)
    f = np.fft.fftshift(np.fft.fftfreq(NFFT, 1 / RATE))
    db = 10 * np.log10(p + 1e-12)
    # median noise floor in a band away from DC and away from strong signals
    band = (np.abs(f) > 50000) & (np.abs(f) < 300000)
    floor = np.median(db[band])
    peak = np.max(db[band])
    return floor, peak

print(f'freq {FREQ} Hz, {SECONDS}s captures')
print(f'{"setting":<22} {"noise floor":>12} {"peak":>8}  {"spread":>7}')
for gain, lna, thr, label in SETTINGS:
    set_gain(gain, lna, thr)
    iq = capture()
    floor, peak = noisefloor(iq)
    print(f'{label:<22} {floor:>10.1f} dB {peak:>8.1f}  {peak-floor:>5.1f} dB')

# reset to AGC
set_gain(0, 0, 0)
