"""Exercise the optional continuous spectrum worker with real USB samples.

Requires the diagnostic firmware and an otherwise unused SpyServer. The two
cases cover simultaneous zoomed IQ and worst-case IQ decimation. The spectrum
stage is chosen at firmware build time, independently of the IQ stage.
"""
import json
import math
import os
import socket
import struct
import threading
import time
import urllib.request

host = os.environ['AIRSPY_HOST']
http = urllib.request.build_opener(urllib.request.ProxyHandler({}))

def get(path):
    with http.open(f'http://{host}{path}', timeout=5) as r:
        return r.read()

def stats(path):
    return json.loads(get(path))

def test(stage):
    before = stats('/api/status')
    assert not before['spy_client'], 'another client is connected'
    sock = socket.create_connection((host, 5555), timeout=5)
    sock.settimeout(5)
    def exact(n):
        data = bytearray(n)
        remaining = memoryview(data)
        while remaining:
            count = sock.recv_into(remaining)
            if not count:
                raise EOFError()
            remaining = remaining[count:]
        return data
    def message():
        header = struct.unpack('<IIIII', exact(20))
        assert header[0] == 0x020006a4 and header[4] < 1000000
        return header, exact(header[4])
    def setting(key, value):
        sock.sendall(struct.pack('<IIII', 2, 8, key, value))
    hello = struct.pack('<I', 0x020006a4) + b'spectrum-pipeline-test'
    sock.sendall(struct.pack('<II', 0, len(hello)) + hello)
    message(); message()
    stop = threading.Event()
    errors = []
    bytes_read = [0]
    def reader():
        try:
            while not stop.is_set():
                h, body = message()
                if h[1] & 65535 == 101:
                    bytes_read[0] += len(body)
        except (OSError, EOFError) as e:
            if not stop.is_set():
                errors.append(str(e))
    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    try:
        for key, value in [(0, 1), (100, 2), (102, stage), (103, 0), (101, 7074000), (1, 1)]:
            setting(key, value)
        time.sleep(5)
        start_status = stats('/api/status')
        start = stats('/api/spectrum-stats')
        initial_bytes = bytes_read[0]
        t0 = time.monotonic()
        frame_count = 0
        previous = None
        fft_stage = None
        duration = float(os.environ.get('AIRSPY_TEST_SECONDS', '20'))
        while time.monotonic() - t0 < duration:
            data = get('/api/spectrum-frame')
            if data:
                assert len(data) == 20 + 2048 * 4, len(data)
                center, rate, fft_stage, seq = struct.unpack_from('<IIII', data)
                assert center == 7074000 and rate == 768000 >> fft_stage
                assert previous is None or seq > previous
                bins = struct.unpack_from('<2048f', data, 20)
                assert all(math.isfinite(v) and -300.0 <= v <= 100.0 for v in bins)
                previous = seq
                frame_count += 1
            time.sleep(.15)
        elapsed = time.monotonic() - t0
        end = stats('/api/spectrum-stats')
        final_status = stats('/api/status')
        assert fft_stage is not None, 'no spectrum frames received'
        report = {'iq_stage': stage, 'fft_stage': fft_stage, 'elapsed': round(elapsed, 2),
                  'bytes_per_s': round((bytes_read[0] - initial_bytes) / elapsed),
                  'spectrum_blocks': end['processed'] - start['processed'],
                  'spectrum_dropped': end['dropped'] - start['dropped'],
                  'spectrum_resets': end['resets'] - start['resets'],
                  'spectrum_frames': end['frames'] - start['frames'],
                  'frames_observed': frame_count, 'max_process_us': end['max_process_us'],
                  'usb_dropped': final_status['dropped_blocks'] - start_status['dropped_blocks'],
                  'cpu0_pct': final_status['cpu0_10'] / 10, 'cpu1_pct': final_status['cpu1_10'] / 10,
                  'heap_free': final_status['free_heap']}
        print(json.dumps(report), flush=True)
        assert not errors, errors
        assert final_status['iq_sample_rate'] == 768000 >> stage
        assert abs(report['bytes_per_s'] - (768000 >> stage) * 4) < (768000 >> stage) * 4 * .03
        assert report['usb_dropped'] == 0 and report['spectrum_dropped'] == 0
        assert report['spectrum_resets'] == 0
        assert report['spectrum_frames'] >= duration * 7 and frame_count >= duration * 4
        setting(1, 0)
    finally:
        stop.set()
        try: sock.shutdown(socket.SHUT_RDWR)
        except OSError: pass
        sock.close(); thread.join(timeout=2)
    time.sleep(.5)

if __name__ == '__main__':
    for stage in (3, 6):
        test(stage)
