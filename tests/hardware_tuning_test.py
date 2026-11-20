"""Hardware test: one free SpyServer client slot and stable HF spectrum required.
Uses a temporary direct connection; install NumPy on the test computer.
"""
import os,socket,struct,time,json,urllib.request
import numpy as np
host=os.environ['AIRSPY_HOST']
op=urllib.request.build_opener(urllib.request.ProxyHandler({}))
def stats(): return json.load(op.open(f'http://{host}/api/status',timeout=5))
s=socket.create_connection((host,5555),timeout=5)
def send(c,b=b''):s.sendall(struct.pack('<II',c,len(b))+b)
def setting(k,v):send(2,struct.pack('<II',k,v))
def exact(n):
    out=bytearray(n);view=memoryview(out)
    while view:
        got=s.recv_into(view)
        if not got:raise EOFError()
        view=view[got:]
    return out

def msg():
    h=struct.unpack('<IIIII',exact(20));assert h[4]<1000000
    return h,exact(h[4])
send(0,struct.pack('<I',0x020006a4)+b'frequency-test')
msg();msg()
setting(0,1);setting(100,2);setting(102,0)
before=stats(); captures=[];results=[]
try:
    setting(1,1)
    for freq in [7074000,7074375,7073625,7074000]:
        setting(101,freq)
        deadline=time.monotonic()+1.4;start=time.monotonic();raw=[];synced=False
        while time.monotonic()<deadline:
            h,b=msg()
            if (h[1]&0xffff)==101 and time.monotonic()-start>0.3:raw.append(b)
        captures.append((freq,b''.join(raw)))
    setting(1,0);s.close()
    after=stats()
    lost=after['dropped_blocks']-before['dropped_blocks']
    print('DROPPED_DELTA',lost,flush=True)
    assert lost==0, f'{lost} USB blocks lost during capture'
    for freq,raw in captures:
        data=np.frombuffer(raw,dtype='<i2').astype(np.float32).reshape(-1,2)
        iq=data[:,0]+1j*data[:,1];n=32768;iq=iq[:len(iq)//n*n].reshape(-1,n)
        p=np.mean(abs(np.fft.fft(iq*np.hanning(n)))**2,axis=0)
        results.append(p)
        print('CAPTURE',freq,'samples',iq.size,flush=True)
    ref=results[0]
    for p,expected in zip(results[1:],[ -16,16,0]):
        # Compare stable narrowband RF structure; whitening removes broad receiver passband.
        a=np.log10(ref+1);b=np.log10(p+1)
        def whiten(x):return x-sum(np.roll(x,i) for i in range(-32,33))/65
        a=whiten(a);b=whiten(b)
        scores=[np.dot(np.roll(a,k),b) for k in range(-32,33)]
        shift=int(np.argmax(scores))-32
        print('SPECTRUM_SHIFT_BINS',shift,'expected',expected,'bin_hz',768000/32768,flush=True)
        assert abs(shift-expected)<=1,(shift,expected)

finally:
    s.close()
