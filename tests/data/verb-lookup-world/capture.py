# Local, owned, synthetic-session capture. Credentials are never retained.
import json,select,socket,struct,sys,time
from pathlib import Path
out=Path(sys.argv[1]);port=int(sys.argv[2]);target=int(sys.argv[3])
def decrypt(body,key):
 if not body:return b''
 total=0;data=bytearray()
 for c in body[:-1]:
  p=(c-total-((key>>(total&31))&255))&255;data.append(p);total=(total+p)&255
 if body[-1]!=total:raise ValueError('Checksum mismatch')
 return bytes(data)
with socket.socket() as listener, out.open('w') as log:
 listener.bind(('127.0.0.1',port));listener.listen(1);print('READY',flush=True)
 client,_=listener.accept();server=socket.create_connection(('127.0.0.1',target))
 buffers={client:bytearray(),server:bytearray()};key=None;base=None;started=time.monotonic();first=True
 with client,server:
  running=True
  while running:
   for source in select.select([client,server],[],[])[0]:
    data=source.recv(65536)
    if not data:running=False;break
    (server if source is client else client).sendall(data)
    b=buffers[source];b.extend(data)
    while True:
     prefix=2 if source is client and not first else 0
     if len(b)<prefix+4:break
     typ,n=struct.unpack('>HH',b[prefix:prefix+4]);header=prefix+4
     if typ==0xa0 and n>=6:
      if len(b)<prefix+10:break
      typ,n=struct.unpack('>HI',b[prefix+4:prefix+10]);header=prefix+10
     if n>0xffffff:raise ValueError('Bad frame length')
     if len(b)<header+n:break
     body=bytes(b[header:header+n]);del b[:header+n]
     direction='c2s' if source is client else 's2c'
     if source is client and first:
      if typ!=1:raise ValueError('Expected handshake')
      proto,f2,f3=struct.unpack('<III',body[:12]);base=(f3+(f2<<16)+proto)&0xffffffff;first=False;continue
     if source is server and key is None and typ==1:
      plain=decrypt(body,base);nonce=plain[11:];words=[int.from_bytes(nonce[i:i+4],'little') for i in range(0,len(nonce)-3,4)]
      i=0
      while i<len(words) and (((words[i]+0x71bd632f)&0xffffffff)>>1)&0x02004000:i+=1
      key=(base+(words[i+1] if i+1<len(words) else 0))&0xffffffff
      log.write(json.dumps({'dir':'s2c','type':'0001-header','body':plain[:11].hex()})+'\n');log.flush();continue
     if typ==0x36:raise ValueError('Certificate rekey not supported; capture aborted without retaining credentials')
     if typ==0x1a:continue
     plain=decrypt(body,key)
     row={'seconds':round(time.monotonic()-started,3),'dir':direction,'type':f'{typ:04x}','size':len(plain)}
     # Narrow payload retention; other c2s metadata locates unknown command messages.
     if (source is server and (typ in [0x11,0x3b,0x39,0x3f] or (len(plain)<=64 and typ not in [0x1c]))) or (source is client and (typ in [2,0x39,0x3f] or b'Probe' in plain)):
      row['body']=plain.hex()
     if source is client or 'body' in row:
      log.write(json.dumps(row)+'\n');log.flush()
