"""Regression for USB control-line reset and bounded console transfers."""
from pathlib import Path
import sys,types,tempfile
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from tab5_usb import Tab5
class Serial:
 def __init__(self,port=None,**kwargs):
  self.opened=False;self.resets=0;self.rts=True;self.dtr=True;self.writes=[]
  if port:self.open()
 def __setattr__(self,key,value):
  if key=='dtr' and value is False and self.__dict__.get('opened') and self.rts:self.resets+=1
  object.__setattr__(self,key,value)
 def open(self):self.opened=True
 def close(self):self.opened=False
 def write(self,data):self.writes.append(data)
 def reset_input_buffer(self):pass
 def readline(self):return b'TAB5 {"error":0}\n'
with patch.dict(sys.modules,serial=types.SimpleNamespace(Serial=Serial)),patch('tab5_usb.time.sleep'):
 device=Tab5('test');assert device.serial.resets==0
 assert not device.command('status')['error']
 try:device.command('read','/sdcard/\ncommand');raise AssertionError('newline accepted')
 except ValueError:pass
 commands=[]
 def command(*args):commands.append(args);return dict(error=0,message='/sdcard/incoming/test.bin')
 device.command=command
 with tempfile.TemporaryDirectory() as tmp:
  file=Path(tmp)/'test.bin';file.write_bytes(bytes(range(256))*2);device.upload(file)
 chunks=[c for c in commands if c[0]=='chunk']
 assert [c[1] for c in chunks]==list(range(0,512,64))
 assert all(len(c[2])<=128 for c in chunks)
 device.close()
print('USB client: no control-line reset, control-character rejection, 64-byte chunks and exact offsets passed.')
