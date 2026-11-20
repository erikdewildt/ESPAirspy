"""Measure every advertised IQ rate. Requires an otherwise free SpyServer."""
import json,os,socket,struct,threading,time,urllib.request
host=os.environ['AIRSPY_HOST']
op=urllib.request.build_opener(urllib.request.ProxyHandler({}))
def stats():
    with op.open(f'http://{host}/api/status', timeout=5) as response:
        return json.load(response)
def test(stage):
    s=socket.create_connection((host,5555),timeout=5)
    def exact(n):
        b=bytearray(n);view=memoryview(b)
        while view:
            got=s.recv_into(view)
            if not got:raise EOFError()
            view=view[got:]
        return b
    def msg():
        h=struct.unpack('<IIIII',exact(20));assert h[0]==0x020006a4 and h[4]<1000000
        return h,exact(h[4])
    def setting(k,v):s.sendall(struct.pack('<IIII',2,8,k,v))
    hello=struct.pack('<I',0x020006a4)+b'rate-test'
    s.sendall(struct.pack('<II',0,len(hello))+hello)
    h,info=msg();assert struct.unpack('<12I',info)[4]==6
    msg()
    received=0;errors=[];stop=threading.Event();lock=threading.Lock()
    def reader():
        nonlocal received
        previous_sequence = None
        try:
            while not stop.is_set():
                h,b=msg()
                if (h[1]&65535)==101:
                    assert h[2]==1 and len(b)%4==0
                    if previous_sequence is not None:
                        assert h[3] == (previous_sequence+1) & 0xffffffff
                    previous_sequence = h[3]
                    assert h[1]>>16==int(os.environ.get('AIRSPY_TEST_GAIN_DB','0'))
                    with lock:received+=len(b)
        except (OSError,EOFError) as e:
            if not stop.is_set():errors.append(str(e))
        except Exception as e:errors.append(repr(e))
    thread=threading.Thread(target=reader,daemon=True);thread.start()
    try:
        for k,v in [(0,1),(100,2),(102,stage),(103,int(os.environ.get("AIRSPY_TEST_GAIN_DB","0"))),(101,7074000),(1,1)]:setting(k,v)
        time.sleep(6)
        a=stats()
        with lock:before=received
        start=time.monotonic();time.sleep(float(os.environ.get("AIRSPY_TEST_SECONDS","10")))
        with lock:after=received
        elapsed=time.monotonic()-start;b=stats()
        rate=768000>>stage;throughput=(after-before)/elapsed
        assert b['uptime_s'] >= a['uptime_s'], 'Device restarted during measurement'
        result = {
            'version': b['version'],
            'stage': stage,
            'sample_rate': rate,
            'payload_bytes_s': round(throughput),
            'expected_bytes_s': rate*4,
            'drops': b['dropped_blocks']-a['dropped_blocks'],
            'cpu0_pct': b['cpu0_10']/10,
            'cpu1_pct': b['cpu1_10']/10,
            'heap_free': b['free_heap'],
        }
        for key in ('input_iq_samples', 'iq_samples_sent', 'iq_ring_lost_samples',
                    'iq_empty_reads', 'iq_send_failures'):
            if key in a and key in b:
                result[f'{key}_delta'] = (b[key]-a[key]) & 0xffffffff if key in (
                    'iq_empty_reads', 'iq_send_failures') else b[key]-a[key]
        for key in ('iq_ring_capacity_bytes', 'iq_ring_peak_estimate_bytes', 'iq_max_send_us', 'heap_min'):
            if key in b:
                result[key] = b[key]
        print(json.dumps(result), flush=True)
        assert not errors,errors
        assert b['iq_sample_rate']==rate
        assert abs(throughput-rate*4)<rate*4*.02,(throughput,rate*4)
        assert b['dropped_blocks']==a['dropped_blocks'],(a,b)
        setting(1,0);time.sleep(.1)
    finally:
        stop.set();s.shutdown(socket.SHUT_RDWR);s.close();thread.join(timeout=2)
    time.sleep(.2)
if __name__=='__main__':
    stages=[int(x) for x in os.environ.get('AIRSPY_TEST_STAGES','0,1,2,3,4,5,6').split(',')]
    for stage in stages:test(stage)
