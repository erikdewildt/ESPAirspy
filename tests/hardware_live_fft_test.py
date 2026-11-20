"""Temporary CONFIG_AIRSPY_FFT_CAPTURE_TEST build: compare live FFT with NumPy."""
import os,socket,struct,threading,time,json,urllib.request
import numpy as np
host=os.environ['AIRSPY_HOST']
op=urllib.request.build_opener(urllib.request.ProxyHandler({}))
def get(path):
 with op.open(f'http://{host}{path}',timeout=5) as r:return r.read()
def status():return json.loads(get('/api/status'))
before=status();assert not before['spy_client']
s=socket.create_connection((host,5555),timeout=5)
def exact(n):
 b=bytearray(n);v=memoryview(b)
 while v:
  got=s.recv_into(v)
  if not got:raise EOFError()
  v=v[got:]
 return b
def msg():
 h=struct.unpack('<IIIII',exact(20));assert h[4]<1000000
 return h,exact(h[4])
def setting(k,v):s.sendall(struct.pack('<IIII',2,8,k,v))
hello=struct.pack('<I',0x020006a4)+b'live-fft-test';s.sendall(struct.pack('<II',0,len(hello))+hello);msg();msg()
stop=threading.Event();errors=[];received=0
def reader():
 global received
 try:
  while not stop.is_set():
   h,b=msg()
   if h[1]&65535==101:received+=len(b)
 except (EOFError,OSError) as e:
  if not stop.is_set():errors.append(str(e))
thread=threading.Thread(target=reader,daemon=True);thread.start()
try:
 for k,v in [(0,1),(100,2),(102,6),(103,18),(101,7074000),(1,1)]:setting(k,v)
 last=-1;frames=0;worst=0;last_query=0;until=time.monotonic()+20
 while time.monotonic()<until:
  data=get('/api/fft-test')
  if not data:time.sleep(.05);continue
  assert len(data)==8+2048+8192,len(data)
  center,seq=struct.unpack_from('<II',data);assert center==7074000 and seq>last,(center,seq,last);last=seq
  actual=np.frombuffer(data,dtype=np.uint8,count=2048,offset=8).astype(np.int16)
  iq=np.frombuffer(data,dtype='<i2',count=4096,offset=8+2048).reshape(-1,2).astype(np.float64)
  z=(iq[:,0]+1j*iq[:,1])*np.hanning(2048)
  power=np.abs(np.fft.fftshift(np.fft.fft(z))/(32768*np.hanning(2048).sum()))**2
  db=10*np.log10(np.maximum(power,1e-20));reference=np.clip(np.floor(255*(db+150)/150+.5),0,255).astype(np.int16)
  strong=reference>=80
  assert strong.any(), 'No measurable spectrum bins'
  error=np.abs(actual-reference);worst=max(worst,int(error[strong].max()))
  assert error[strong].max()<=3,(seq,int(error[strong].max()))
  assert abs(int(actual.argmax())-int(reference.argmax()))<=1
  frames+=1
  if time.monotonic()-last_query>3:
   assert 'samples' in json.loads(get('/api/history?range=1d'));last_query=time.monotonic()
  time.sleep(.2)
 after=status();assert not errors,errors
 assert frames>=30,frames
 assert after['dropped_blocks']==before['dropped_blocks'],(before['dropped_blocks'],after['dropped_blocks'])
 assert received>600000,received
 print('PASS live spectra:',frames,'last sequence',last,'max strong-bin difference',worst,'USB drops',after['dropped_blocks']-before['dropped_blocks'],'IQ bytes',received,'cores',after['cpu0_10']/10,after['cpu1_10']/10,flush=True)
 setting(1,0)
finally:
 stop.set();s.shutdown(socket.SHUT_RDWR);s.close();thread.join(timeout=2)
