#!/usr/bin/env python3
"""Return to launcher after ejecting the Tab5 microSD in the computer.
Requires pyusb and libusb. RESET on the Tab5 is the hardware alternative.
"""
import argparse
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--ejected',action='store_true',help='Confirm OS ejection/unmount has completed');a=p.parse_args()
if not a.ejected:p.error('Eject/unmount the Tab5 disk first, then pass --ejected')
import usb.core,usb.util
matches=[]
for device in usb.core.find(find_all=True,idVendor=0x303a,idProduct=0x4002):
 if usb.util.get_string(device,device.iProduct)=='Tab5 microSD':matches.append(device)
if len(matches)!=1:raise SystemExit('Expected one Tab5 microSD USB device; found '+str(len(matches)))
matches[0].ctrl_transfer(0x40,0x5a,0x5442,0,None,timeout=3000)
print('Return requested. The USB Serial/JTAG console will reappear.')
