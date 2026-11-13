"""Fail if launcher source or linked firmware uses NVS persistence.
The conventional application-only NVS partition is allowed; launcher data stays on SD.
Third-party SDKs can compile an unused NVS library. The empty SDK linker hook is
allowed; initialization, key access and partition access APIs are not.
"""
from pathlib import Path
import argparse,json,re,shlex,shutil,subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build',type=Path,default=Path('build'))
a=p.parse_args();root=Path(__file__).resolve().parents[1]
errors=[]
for folder in ('components','main'):
 for path in (root/folder).rglob('*'):
  if any(part.startswith('espressif__') for part in path.parts):continue
  if path.suffix not in ('.c','.h') and path.name!='CMakeLists.txt':continue
  text=path.read_text()
  if re.search(r'\bnvs_[A-Za-z0-9_]+\s*\(',text) or re.search(r'#\s*include\s*[<"]nvs(?:_flash)?\.h',text) or re.search(r'\bnvs_flash\b',text):errors.append(str(path.relative_to(root)))
for row in (root/'partitions.csv').read_text().splitlines():
 if not row.strip() or row.lstrip().startswith('#'):continue
 cols=[x.strip() for x in row.split(',')]
 if len(cols)>2 and cols[1]=='data' and cols[2] in ('nvs','nvs_keys','0x02','0x04'):
  application_nvs = len(cols)>4 and cols[0]=='nvs' and cols[2]=='nvs' and int(cols[3],0)==0x9000 and int(cols[4],0)==0x6000
  if not application_nvs: errors.append('unexpected NVS reservation: '+cols[0])
commands=json.loads((a.build/'compile_commands.json').read_text())
compiler=next((shlex.split(x['command'])[0] for x in commands if x['file'].endswith('/main/main.c')),None)
nm=compiler.rsplit('gcc',1)[0]+'nm' if compiler and compiler.endswith('gcc') else shutil.which('nm')
if not nm:raise SystemExit('Cannot locate nm for firmware inspection')
elf=a.build/'tab5_launcher.elf'
for line in subprocess.check_output([nm,'--defined-only',str(elf)],text=True).splitlines():
 parts=line.split()
 if len(parts)>=3 and parts[-2] in ('T','t') and parts[-1].startswith('nvs_') and parts[-1]!='nvs_sec_provider_include_impl':errors.append('linked NVS routine: '+parts[-1])
if errors:raise SystemExit('\n'.join(errors))
print('No launcher NVS: no persistence APIs/includes, conventional application-only NVS allowed, no linked NVS routines (empty SDK linker hook excepted).')
