"""Check compiler-measured UI frames after idf.py build (-fstack-usage).
Worker tasks have separate, larger stacks. UI callbacks run on taskLVGL.
"""
from pathlib import Path
import argparse
p=argparse.ArgumentParser(description=__doc__); p.add_argument('--build',type=Path,default=Path('build')); a=p.parse_args()
rows=[]
for path in (a.build/'esp-idf/ui').rglob('*.su'):
 for line in path.read_text().splitlines():
  fields=line.split('\t')
  if len(fields)<3: continue
  location,frame,kind=fields[:3]
  try: frame=int(frame)
  except ValueError: continue
  name=location.rsplit(':',1)[-1]
  if name=='worker': continue  # explicitly sized service tasks, not LVGL callbacks
  limit=512 if 'ui_sd.c:' in location and name=='submit' else 3072
  if frame>limit or ('dynamic' in kind and 'bounded' not in kind):
   raise SystemExit(f'UI stack frame exceeds budget: {location}: {frame} bytes ({kind}), limit {limit}')
  rows.append((frame,location))
if not rows: raise SystemExit('Missing .su files; build the UI with -fstack-usage first')
maximum=max(rows)
print(f'UI stack budget OK: {len(rows)} functions; largest non-worker frame {maximum[0]} bytes ({maximum[1]}).')
