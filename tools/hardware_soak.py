#!/usr/bin/env python3
"""Read-only USB soak recorder. Hot-plug/power changes are performed by the operator.
Does not claim battery calibration or reboot recovery from successful polling.
"""
import argparse,json,time,sys
from pathlib import Path
from tab5_usb import Tab5

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port',required=True);p.add_argument('--seconds',type=int,default=3600);p.add_argument('--interval',type=float,default=10);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if not 1<=a.seconds<=86400 or a.interval<2:p.error('Use 1..86400 seconds and interval >=2')
 samples=0;errors=0;started=time.monotonic();c=None
 with a.output.open('x') as log:
  while time.monotonic()-started<a.seconds:
   record={'elapsed':round(time.monotonic()-started,2),'utc':time.time()}
   try:
    if c is None:c=Tab5(a.port,timeout=30)
    record['status']=c.command('status');record['sdListing']=c.command('ls','/sdcard',0);samples+=1
   except Exception as e:
    record['error']=str(e);errors+=1
    if c:c.close();c=None
   log.write(json.dumps(record,ensure_ascii=False)+'\n');log.flush()
   time.sleep(min(a.interval,max(0,a.seconds-(time.monotonic()-started))))
 if c:c.close()
 print(json.dumps({'samples':samples,'errors':errors,'seconds':round(time.monotonic()-started,2),'log':str(a.output)}))
 if errors:sys.exit(1)
if __name__=='__main__':main()
