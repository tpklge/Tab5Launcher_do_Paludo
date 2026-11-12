from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(root/'tools'),str(root/'tests/firmware')]
from prepare_dynamic_upgrade import prepare,table
from fixtures import image
backup=bytearray(b'\xff'*0x1000000)
backup[0x8000:0x8c00]=(root/'tests/fixtures/legacy-partition-table.bin').read_bytes()
launcher=image();one=image(version=b'1');two=image(version=b'2')
backup[0x210000:0x210000+len(one)]=one
backup[0x610000:0x610000+len(two)]=two
backup[0x613000:0x613004]=b'DATA'
target,bank,report=prepare(bytes(backup),launcher);rows=table(target)
assert report['launcher_size']==65536 and len(report['apps'])==2
assert rows['app0'][2:4]==(0x30000,65536) and rows['app1'][2:4]==(0x40000,65536)
assert bank[:len(one)]==one and bank[0x10000:0x10000+len(two)]==two
assert bank[0x13000:0x13004]==b'DATA'
for name,value in table(backup[0x8000:0x9000]).items():
 if value[0]!=0:assert rows[name]==value
bad=bytearray(launcher);bad[100]^=1
for original,app in [(bytes(backup),bytes(bad)),(bytes(backup[:-1]),launcher),(bytes(backup),image(chip=0x10))]:
 try:prepare(original,app)
 except ValueError:continue
 raise AssertionError('Invalid upgrade accepted')
print('Dynamic upgrade: minimum alignment, preserved apps and appended data, unchanged data entries, wrong chip/hash/truncated backup rejection passed.')
