#!/usr/bin/env python3
"""Tab5 maintenance client over USB Serial/JTAG (also runs on Raspberry Pi).
Close idf_monitor before use. Never flashes firmware or changes partition tables.
"""
import argparse
import hashlib
import json
import time
from pathlib import Path

def file_sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as source:
        for data in iter(lambda:source.read(65536),b''):h.update(data)
    return h.hexdigest()

class Tab5:
    def __init__(self, port, timeout=30):
        import serial
        # Match IDF monitor's line order. Releasing DTR before RTS resets the
        # USB Serial/JTAG device and can discard the first transfer command.
        self.serial=serial.Serial(port=None,baudrate=115200,timeout=.25,write_timeout=5)
        self.serial.rts=True
        self.serial.dtr=True
        self.serial.port=port
        self.serial.open()
        self.serial.rts=False
        self.serial.dtr=False
        self.timeout=timeout
        self.serial.write(b'\r\n')
        time.sleep(.3)
        self.serial.reset_input_buffer()
    def close(self): self.serial.close()
    def command(self,*args):
        # IDF console understands double-quoted strings and backslash escapes.
        def quote(arg):
            value=str(arg)
            if any(ord(c)<32 for c in value): raise ValueError('Control characters rejected')
            return '"'+value.replace('\\','\\\\').replace('"','\\"')+'"'
        line='tab5 '+' '.join(quote(a) for a in args)+'\r\n'
        if len(line.encode())>=768: raise ValueError('Command too long')
        self.serial.write(line.encode())
        deadline=time.monotonic()+self.timeout
        while time.monotonic()<deadline:
            line=self.serial.readline().decode(errors='replace')
            marker=line.find('TAB5 {')
            if marker<0: continue
            try: reply=json.loads(line[marker+5:])
            except json.JSONDecodeError: continue
            if reply.get('error'): raise RuntimeError(reply)
            return reply
        raise TimeoutError('Tab5 did not answer; console unavailable or operation still running')
    def upload(self,path):
        path=Path(path)
        if not path.is_file() or not 0<path.stat().st_size<=16*1024*1024: raise ValueError('File must be 1 byte to 16 MiB')
        digest=file_sha(path)
        self.command('begin',path.name,path.stat().st_size,digest)
        try:
            with path.open('rb') as f:
                offset=0
                while block:=f.read(64):
                    self.command('chunk',offset,block.hex());offset+=len(block)
            return self.command('commit')
        except BaseException:
            try: self.command('abort')
            except Exception: pass
            raise
    def download(self,remote,local):
        local=Path(local)
        # Exclusive creation: never truncate existing host files.
        try:
            with local.open('xb') as f:
                offset=0
                while True:
                    reply=self.command('read',remote,offset,256)
                    if reply.get('offset')!=offset: raise ValueError('Reply offset mismatch')
                    data=bytes.fromhex(reply['hex'])
                    if len(data)>256: raise ValueError('Oversized reply')
                    f.write(data);offset+=len(data)
                    if len(data)<256: break
        except BaseException:
            # Keep incomplete download explicit; do not claim successful transfer.
            raise
        digest=file_sha(local)
        return {'path':str(local),'size':offset,'sha256':digest}
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',required=True)
    sub=p.add_subparsers(dest='cmd',required=True)
    sub.add_parser('status')
    web=sub.add_parser('install-web');web.add_argument('directory',type=Path)
    sub.add_parser('screenshot')
    ls=sub.add_parser('ls');ls.add_argument('path',nargs='?',default='/sdcard');ls.add_argument('--page',type=int,default=0)
    up=sub.add_parser('upload');up.add_argument('file')
    down=sub.add_parser('download');down.add_argument('remote');down.add_argument('local')
    backup=sub.add_parser('backup');backup.add_argument('slot',type=int)
    boot=sub.add_parser('boot');boot.add_argument('slot',type=int)
    sub.add_parser('abort')
    a=p.parse_args();device=Tab5(a.port,timeout=180 if a.cmd=='backup' else 30)
    try:
        if a.cmd=='install-web':
            result=[]
            for name in ('index.html','style.css','app.js','transfers.js'):
                uploaded=device.upload(a.directory/name)
                result.append(device.command('asset',uploaded['message'],name))
        elif a.cmd=='upload': result=device.upload(a.file)
        elif a.cmd=='download': result=device.download(a.remote,a.local)
        elif a.cmd=='ls': result=device.command('ls',a.path,a.page)
        elif a.cmd in ('boot','backup'): result=device.command(a.cmd,a.slot)
        else: result=device.command(a.cmd)
        print(json.dumps(result,ensure_ascii=False,indent=2))
    finally: device.close()
if __name__=='__main__':main()
