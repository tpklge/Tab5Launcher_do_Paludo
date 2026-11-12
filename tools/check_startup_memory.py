"""Prevent large UI caches from returning to scarce pre-scheduler internal RAM.
Run after idf.py build in the activated ESP-IDF environment.
"""
from pathlib import Path
import argparse,subprocess
p=argparse.ArgumentParser(description=__doc__); p.add_argument('--elf',default='build/tab5_launcher.elf'); p.add_argument('--nm',default='riscv32-esp-elf-nm'); a=p.parse_args()
output=subprocess.check_output([a.nm,'-S','--defined-only',a.elf],text=True)
required={'s_model','s_listing','s_plan','s_editor_original','s_editor_path','s_log'}; found={}
for line in output.splitlines():
 fields=line.split()
 if len(fields)==4 and fields[3] in required:
  address,size=int(fields[0],16),int(fields[1],16)
  if not 0x48000000<=address<0x4a000000 or size>0x4a000000-address: raise SystemExit(f'{fields[3]} must remain in PSRAM')
  found[fields[3]]=size
if set(found)!=required: raise SystemExit('Missing expected UI cache symbols')
print(f'Startup memory OK: {sum(found.values())} bytes of UI caches placed in PSRAM.')

# ESP32-P4 low DRAM must leave room for pre-scheduler main/idle/timer stacks.
# This bound includes every component, rather than checking only known caches.
for line in output.splitlines():
 fields=line.split()
 if len(fields)==3 and fields[2]=='_heap_start_low':
  address=int(fields[0],16)
  if address>0x4ff1f000:raise SystemExit(f'Pre-scheduler DRAM budget exceeded: heap starts at {address:#x}; maximum 0x4ff1f000')
  print(f'Pre-scheduler DRAM budget OK: heap starts at {address:#x}.')
