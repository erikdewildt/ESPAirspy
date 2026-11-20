"""Ensure control replies do not create false gaps in I/Q sequence numbers."""
import os,socket,struct
host=os.environ['AIRSPY_HOST']
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
def setting(k,v):s.sendall(struct.pack('<IIII',2,8,k,v))
hello=struct.pack('<I',0x020006a4)+b'sequence-test';s.sendall(struct.pack('<II',0,len(hello))+hello);msg();msg()
for k,v in [(0,1),(100,2),(102,3),(101,7074000),(1,1)]:setting(k,v)
last=None;frames=0
def receive():
 global last,frames
 h,b=msg();kind=h[1]&65535
 if kind==101:
  if last is not None:assert h[3]==(last+1)&0xffffffff,(last,h[3])
  last=h[3];frames+=1
 return kind
try:
 while last is None:receive()
 for i in range(5):
  s.sendall(struct.pack('<II',3,0))
  while receive()!=2:pass
  while receive()!=101:pass
  setting(101,7074000+(i%2)*375)
  while receive()!=1:pass
  while receive()!=101:pass
 print('PASS:',frames,'consecutive I/Q frames across five pongs and retune sync messages')
 setting(1,0)
finally:s.close()
