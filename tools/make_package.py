"""Create the launcher's explicit multipart manifest; firmware validates it again.
All components must be alongside the resulting JSON on SD. No boot infrastructure.
"""
from pathlib import Path
import argparse,hashlib,json
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--app',type=Path,required=True)
p.add_argument('--data',action='append',default=[],help='vfs=file.bin or spiffs=file.bin')
p.add_argument('--output',type=Path,required=True)
a=p.parse_args(); folder=a.output.resolve().parent
if a.output.exists(): raise SystemExit('Output exists; refusing to overwrite')
def entry(path):
 path=path.resolve()
 if path.parent!=folder or not path.is_file() or not 0<path.stat().st_size<=16*1024*1024:
  raise SystemExit('Each component must be a nonempty file <=16 MB alongside the output manifest')
 digest=hashlib.sha256()
 with path.open('rb') as f:
  for chunk in iter(lambda:f.read(65536),b''): digest.update(chunk)
 return {'file':path.name,'sha256':digest.hexdigest()}
manifest={'schema':1,'chip':'esp32p4','app':entry(a.app),'data':[]}
labels=set()
for value in a.data:
 label,separator,name=value.partition('=')
 if not separator or label not in ('vfs','spiffs') or label in labels: raise SystemExit('Data labels must be unique vfs/spiffs')
 labels.add(label); manifest['data'].append(dict(entry(Path(name)),label=label))
a.output.write_text(json.dumps(manifest,indent=2)+'\n')
print(a.output)
