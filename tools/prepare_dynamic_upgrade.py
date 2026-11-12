"""Prepare a minimal launcher layout from a full backup. Never flashes a device.
Preserves data partitions and every occupied app, including appended partition data.
"""
from pathlib import Path
import argparse, hashlib, json, struct
from prepare_multiboot_upgrade import table

LIMIT=0xa10000

def image_length(data):
    if len(data)<24 or data[0]!=0xe9 or not 1<=data[1]<=16 or data[23]!=1 or struct.unpack_from("<H",data,12)[0]!=0x12:
        raise ValueError('Expected SHA256-protected ESP image')
    off=24; checksum=0xef
    for _ in range(data[1]):
        if off+8>len(data): raise ValueError('Truncated image')
        _,size=struct.unpack_from('<II',data,off);off+=8
        if size>len(data)-off:raise ValueError('Truncated segment')
        for byte in data[off:off+size]:checksum^=byte
        off+=size
    padded=(off+16)&~15
    if padded+32>len(data) or data[padded-1]!=checksum or hashlib.sha256(data[:padded]).digest()!=data[padded:padded+32]:
        raise ValueError('Invalid image checksum/hash')
    return padded+32

def prepare(backup,launcher):
    if len(backup)!=0x1000000:raise ValueError('Expected a complete 16 MiB backup')
    old=table(backup[0x8000:0x9000]);length=image_length(launcher)
    if length!=len(launcher):raise ValueError('Unexpected bytes after launcher image')
    root=next((name for name,p in old.items() if p[:3]==(0,0,0x20000)),None)
    if not root:raise ValueError('Missing launcher factory partition')
    size=(length+65535)&~65535;address=0x20000+size
    new={};apps=[]
    for name,p in old.items():
        kind,subtype,start,count,flags=p
        if kind==0 and 16<=subtype<=31:
            if start<0x20000+old[root][3] or start+count>LIMIT or flags:
                raise ValueError('Unsupported app bank')
            raw=backup[start:start+count];used=len(raw.rstrip(b'\xff'))
            if used:
                image_length(raw)
                apps.append((subtype,name,raw[:(used+4095)&~4095]))
        elif name==root:new[name]=(kind,subtype,start,size,flags)
        else:
            if start<LIMIT and start+count>0x20000:raise ValueError("Unexpected data inside application bank")
            new[name]=p
    bank=bytearray(b'\xff'*(LIMIT-address));moves=[]
    for subtype,name,raw in sorted(apps):
        reserve=(len(raw)+65535)&~65535
        if address+reserve>LIMIT:raise ValueError('Insufficient flash; remove apps before updating')
        bank[address-0x20000-size:address-0x20000-size+len(raw)]=raw
        new[name]=(0,subtype,address,reserve,0)
        moves.append({'name':name,'old':old[name][2],'new':address,'size':len(raw),'sha256':hashlib.sha256(raw).hexdigest()})
        address+=reserve
    entries=bytearray()
    for name,p in sorted(new.items(),key=lambda x:x[1][2]):
        kind,subtype,start,count,flags=p
        if not count or start<0x9000 or start+count>len(backup):raise ValueError('Invalid partition')
        entries+=struct.pack('<HBBII16sI',0x50aa,kind,subtype,start,count,name.encode(),flags)
    parsed=table(bytes(entries)+b'\xeb\xeb'+b'\xff'*14+hashlib.md5(entries).digest())
    rows=sorted(parsed.values(),key=lambda p:p[2])
    if any(a[2]+a[3]>b[2] for a,b in zip(rows,rows[1:])):raise ValueError('Overlapping partitions')
    result=bytes(entries)+b'\xeb\xeb'+b'\xff'*14+hashlib.md5(entries).digest()
    result=result.ljust(4096,b'\xff')
    return result,bytes(bank),{'launcher_size':size,'free_bytes':LIMIT-address,'apps':moves,'backup_sha256':hashlib.sha256(backup).hexdigest()}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('backup',type=Path);p.add_argument('--app',type=Path,default=Path('build/tab5_launcher.bin'));p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    raw=a.backup.read_bytes();app=a.app.read_bytes();t,b,report=prepare(raw,app);a.out.mkdir(parents=True,exist_ok=True)
    files=[(0x20000,'launcher.bin',app),(0x20000+report['launcher_size'],'app-bank.bin',b),(0x13000,'partition-mirror.bin',t),(0x8000,'partition-table.bin',t)]
    report['writes']=[]
    for offset,name,data in files:
        (a.out/name).write_bytes(data);report['writes'].append({'offset':offset,'file':name,'size':len(data),'sha256':hashlib.sha256(data).hexdigest()})
    (a.out/'plan.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
