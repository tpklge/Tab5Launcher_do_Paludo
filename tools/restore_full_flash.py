"""Validate a 16 MiB Tab5 snapshot. Cross-version restore uses USB ROM mode.
Default is read-only. Writing requires --write, an exact --confirm-sha256 and
an exclusive destination for the mandatory current-device backup.
"""
from pathlib import Path
import argparse,hashlib,json,struct,subprocess,sys
from prepare_multiboot_upgrade import table

def image(data,offset,capacity):
 b=data[offset:offset+capacity]
 if len(b)<24 or b[0]!=0xe9 or not 1<=b[1]<=16 or b[23] not in (0,1) or struct.unpack_from('<H',b,12)[0]!=0x12:raise ValueError('Expected ESP32-P4 bootable image')
 at=24;checksum=0xef
 for _ in range(b[1]):
  if at+8>len(b):raise ValueError('Truncated image segment')
  _,size=struct.unpack_from('<II',b,at);at+=8
  if size>len(b)-at:raise ValueError('Image segment outside partition')
  for value in b[at:at+size]:checksum^=value
  at+=size
 end=(at+16)&~15
 if end>len(b) or b[end-1]!=checksum:raise ValueError('Image checksum mismatch')
 if b[23] and (end+32>len(b) or hashlib.sha256(b[:end]).digest()!=b[end:end+32]):raise ValueError('Image digest mismatch')
 return end+(32 if b[23] else 0)

def validate(data):
 if len(data)!=0x1000000:raise ValueError('Expected an entire 16 MiB flash snapshot')
 rows=table(data[0x8000:0x9000]);spans=[];factory=None
 for name,(kind,subtype,address,size,flags) in rows.items():
  if not size or address<0x9000 or address+size>len(data) or address%4096 or size%4096 or flags:raise ValueError('Invalid/encrypted partition range')
  if any(address<end and begin<address+size for begin,end in spans):raise ValueError('Partition overlap')
  if kind==0 and address%65536:raise ValueError('Application is not aligned to 64 KiB')
  spans.append((address,address+size))
  if kind==0 and subtype==0:factory=(address,size)
 if factory is None:raise ValueError('Missing factory image')
 boot=image(data,0x2000,0x6000);app=image(data,*factory)
 return dict(sha256=hashlib.sha256(data).hexdigest(),size=len(data),bootloader_size=boot,factory_address=hex(factory[0]),factory_image_size=app,partitions=list(rows))

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('snapshot',type=Path);p.add_argument('--write',action='store_true');p.add_argument('--port');p.add_argument('--confirm-sha256');p.add_argument('--backup',type=Path);a=p.parse_args()
 report=validate(a.snapshot.read_bytes());print(json.dumps(report,indent=2),flush=True)
 if not a.write:return
 if not a.port or not a.backup or a.confirm_sha256!=report['sha256']:p.error('Writing requires --port, --backup and exact --confirm-sha256 from validation')
 if a.backup.exists() or a.backup.with_suffix(a.backup.suffix+'.part').exists():p.error('Backup destination already exists')
 partial=a.backup.with_suffix(a.backup.suffix+'.part');command=[sys.executable,'-m','esptool','--chip','esp32p4','--port',a.port,'--baud','921600']
 subprocess.run(command+['read_flash','0','0x1000000',str(partial)],check=True)
 if partial.stat().st_size!=0x1000000:raise ValueError('Current-device backup is incomplete; restore aborted')
 partial.chmod(0o600);partial.rename(a.backup);a.backup.with_suffix(a.backup.suffix+'.sha256').write_text(hashlib.sha256(a.backup.read_bytes()).hexdigest()+'\n')
 # Read the source again after backing up to reject changes since confirmation.
 if validate(a.snapshot.read_bytes())['sha256']!=report['sha256']:raise ValueError('Snapshot changed; restore aborted')
 subprocess.run(command+['write_flash','--flash_mode','dio','--flash_size','16MB','--flash_freq','80m','0',str(a.snapshot)],check=True)
if __name__=='__main__':main()
