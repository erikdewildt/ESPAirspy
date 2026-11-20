"""Exercise stop/start and retune on a free SpyServer client connection."""
import os,select,socket,struct,time,json,urllib.request
host=os.environ['AIRSPY_HOST']
op=urllib.request.build_opener(urllib.request.ProxyHandler({}))
def stats():return json.load(op.open(f'http://{host}/api/status',timeout=5))
s=socket.create_connection((host,5555),timeout=5)
def exact(n):
 b=bytearray(n);v=memoryview(b)
 while v:
  got=s.recv_into(v)
  if not got:raise EOFError()
  v=v[got:]
 return b
def msg():
 h=struct.unpack('<IIIII',exact(20));assert h[4]<1000000;return h,exact(h[4])
def command(k,v):s.sendall(struct.pack('<IIII',2,8,k,v))
hello=struct.pack('<I',0x020006a4)+b'transition-test';s.sendall(struct.pack('<II',0,len(hello))+hello)
msg();msg();command(0,1);command(100,2);command(102,3);command(103,6)
try:
 for i in range(10):
  target=7074000+(i%2)*375
  command(101,target)
  while True:
   h,b=msg()
   if h[1]&65535==1:
    assert struct.unpack('<9I',b)[3]==target
    assert stats()['frequency']==target
    break
  command(1,1)
  while True:
   h,b=msg()
   if h[1]&65535==101:
    assert h[1]>>16==6 and len(b)>0 and len(b)%4==0
    break
  command(1,0)
  # Previously transmitted frames can still be in the TCP receive queue.
  while select.select([s],[],[],.3)[0]:msg()
  assert not select.select([s],[],[],.3)[0], 'Samples arrived after the stopped stream drained'
  print('cycle',i+1,'center',target,'stop/start OK',flush=True)
finally:s.close()
