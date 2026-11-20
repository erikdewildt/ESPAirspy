"""Capture IQ from the SpyServer and report the spectrum: DC spike and the
strongest peaks with their image (mirror) levels, to assess DC offset and IQ
imbalance on the real hardware."""
import os, socket, struct, time
import numpy as np

host = os.environ['AIRSPY_HOST']
FREQ = int(os.environ.get('AIRSPY_TEST_FREQ', '7074000'))
NFFT = 32768
CAPTURE_SECONDS = float(os.environ.get('AIRSPY_TEST_SECONDS', '4'))

s = socket.create_connection((host, 5555), timeout=5)

def send(c, b=b''):
    s.sendall(struct.pack('<II', c, len(b)) + b)

def setting(k, v):
    send(2, struct.pack('<II', k, v))

def exact(n):
    out = bytearray(n)
    view = memoryview(out)
    while view:
        got = s.recv_into(view)
        if not got:
            raise EOFError()
        view = view[got:]
    return out

def msg():
    h = struct.unpack('<IIIII', exact(20))
    return h, exact(h[4])

send(0, struct.pack('<I', 0x020006a4) + b'sensitivity-test')
msg(); msg()
setting(0, 1); setting(100, 2); setting(102, 0); setting(103, 0)
setting(101, FREQ)
setting(1, 1)

raw = bytearray()
deadline = time.monotonic() + CAPTURE_SECONDS
while time.monotonic() < deadline:
    h, b = msg()
    if (h[1] & 0xffff) == 101 and h[2] == 1:
        raw += b
setting(1, 0)
s.close()

data = np.frombuffer(raw, dtype='<i2').astype(np.float32).reshape(-1, 2)
iq = data[:, 0] + 1j * data[:, 1]
iq = iq[:len(iq) // NFFT * NFFT].reshape(-1, NFFT)
window = np.hanning(NFFT)
p = np.mean(np.abs(np.fft.fft(iq * window, axis=1)) ** 2, axis=0)
p = np.fft.fftshift(p)
freqs = np.fft.fftshift(np.fft.fftfreq(NFFT, 1 / 768000))

def db(x):
    return 10 * np.log10(x + 1e-12)

# DC spike (bin at 0 Hz) and its surroundings
dc = np.where(freqs == 0)[0][0]
print(f'DC bin level: {db(p[dc]):.1f} dB')
print(f'  +/-1 bin: {db(p[dc-1]):.1f} / {db(p[dc+1]):.1f} dB')
print(f'  +/-8 bin: {db(p[dc-8]):.1f} / {db(p[dc+8]):.1f} dB')
print(f'  median (excl DC +/-8): {db(np.median(np.delete(p, range(dc-8, dc+9)))):.1f} dB')
print(f'  DC above median: {db(p[dc]) - db(np.median(np.delete(p, range(dc-8, dc+9)))):.1f} dB')

# Strongest peaks and their images (IQ imbalance -> image leakage)
print('\nTop peaks and image levels (image rejection):')
# exclude DC and very low freq for peak search
mask = np.abs(freqs) > 20000
peaks = np.argsort(p[mask])[::-1]
seen = set()
for idx in peaks:
    f = freqs[mask][idx]
    if abs(f) < 1:
        continue
    if any(abs(f - sf) < 3000 for sf in seen):
        continue
    # image is at -f
    im = np.argmin(np.abs(freqs + f))
    lvl = db(p[mask][idx])
    imlvl = db(p[im])
    print(f'  {f:+.0f} Hz: {lvl:.1f} dB, image @ {-f:+.0f} Hz: {imlvl:.1f} dB, rejection {lvl-imlvl:.1f} dB')
    seen.add(f)
    if len(seen) >= 6:
        break
