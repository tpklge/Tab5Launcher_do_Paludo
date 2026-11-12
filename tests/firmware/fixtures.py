import hashlib
import struct
def image(chip=0x12, revision=100, maximum=199, version=b'1.0', payload=b'\x01\x02\x03\x04'*2048):
    h=bytearray(24); h[0:4]=bytes((0xe9,1,2,0x40)); struct.pack_into('<I',h,4,0x40000020)
    struct.pack_into('<H',h,12,chip); struct.pack_into('<HH',h,15,revision,maximum); h[23]=1
    desc=bytearray(256); struct.pack_into('<I',desc,0,0xabcd5432)
    desc[16:16+len(version)]=version; desc[48:55]=b'TestApp'; desc[112:118]=b'v5.4.4'
    segment=bytes(desc)+payload
    blob=bytes(h)+struct.pack('<II',0x40000020,len(segment))+segment
    checksum=0xef
    for value in segment: checksum ^= value
    blob += bytes((-(len(blob)+1))%16)+bytes((checksum,))
    return blob+hashlib.sha256(blob).digest()
