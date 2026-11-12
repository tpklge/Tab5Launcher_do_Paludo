"""Read-only preflight for the specific 8 MB app0 -> two 4 MB slots upgrade.
Does not flash. Requires a complete current flash backup and the new build table.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct

def table(data):
    rows={}
    for off in range(0,len(data),32):
        entry=data[off:off+32]
        if entry[:2]==b'\xeb\xeb':
            if hashlib.md5(data[:off]).digest()!=entry[16:]:
                raise ValueError('Partition table checksum mismatch')
            return rows
        magic,kind,subtype,address,size,label,flags=struct.unpack('<HBBII16sI',entry)
        if magic!=0x50aa: raise ValueError('Missing checked partition table')
        name=label.split(b'\0')[0].decode('ascii')
        if name in rows: raise ValueError('Duplicate partition')
        rows[name]=(kind,subtype,address,size,flags)
    raise ValueError('Missing table checksum')

def preflight(backup,new_table):
    if len(backup)!=0x1000000: raise ValueError('Expected complete 16 MB flash backup')
    old=table(backup[0x8000:0x9000]); new=table(new_table)
    if old.get('app0')!=(0,16,0x210000,0x800000,0): raise ValueError('Unexpected old app0 layout')
    expected=dict(old); expected['app0']=(0,16,0x210000,0x400000,0); expected['app1']=(0,17,0x610000,0x400000,0)
    if new!=expected: raise ValueError('Unexpected partition change; only splitting app0 is supported')
    if backup[0x610000:0xa10000]!=b'\xff'*0x400000: raise ValueError('Upper half contains data; migration refused')
    image=backup[0x210000:0x610000]
    if image[0]!=0xe9 or not 1<=image[1]<=16 or image[23]!=1 or struct.unpack_from('<H',image,12)[0]!=0x12:
        raise ValueError('Expected SHA256-protected P4 image in app0')
    off=24; checksum=0xef
    for _ in range(image[1]):
        if off+8>len(image): raise ValueError('Image does not fit 4 MB')
        address,size=struct.unpack_from('<II',image,off); off+=8
        if size>len(image)-off: raise ValueError('Image segment does not fit 4 MB')
        for byte in image[off:off+size]: checksum^=byte
        off+=size
    padded=(off+16)&~15
    if padded+32>len(image) or image[padded-1]!=checksum or hashlib.sha256(image[:padded]).digest()!=image[padded:padded+32]:
        raise ValueError('Existing image checksum/hash mismatch')
    return {'backup_sha256':hashlib.sha256(backup).hexdigest(),'table_sha256':hashlib.sha256(new_table).hexdigest(),
            'app0_image_size':padded+32,'app0_image_sha256':hashlib.sha256(image[:padded+32]).hexdigest(),
            'app0_4mb_sha256':hashlib.sha256(image).hexdigest(),'upper_4mb_erased':True,
            'preserved_partitions':[name for name in old if name!='app0'],
            'write_offsets':['0x8000','0x20000']}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('backup',type=Path)
    p.add_argument('--table',type=Path,default=Path('build/partition_table/partition-table.bin'))
    p.add_argument('--report',type=Path,required=True); a=p.parse_args()
    result=preflight(a.backup.read_bytes(),a.table.read_bytes()); a.report.write_text(json.dumps(result,indent=2)+'\n')
    print(f"Preflight OK: app0 {result['app0_image_size']} bytes; upper 4 MB erased; all data offsets preserved.")
