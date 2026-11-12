#!/usr/bin/env python3
"""Rebuild optional SD fonts with lv_font_conv 1.5.3 and Google Noto sources.
Supply the NotoSansSC and NotoEmoji TTFs plus their OFL license files.
"""
import argparse,subprocess,hashlib,json,shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--converter',required=True);p.add_argument('--node',default='node');p.add_argument('--cjk',type=Path,required=True);p.add_argument('--emoji',type=Path,required=True);p.add_argument('--cjk-license',type=Path,required=True);p.add_argument('--emoji-license',type=Path,required=True);p.add_argument('--out',type=Path,default=Path('sdcard/launcher/fonts'));a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
manifest={}
for name,font,license,ranges in [('cjk',a.cjk,a.cjk_license,'0x3000-0x30ff,0x3400-0x4dbf,0x4e00-0x9fff,0xff00-0xffef'),('emoji',a.emoji,a.emoji_license,'0x2300-0x23ff,0x2600-0x27bf,0x1f000-0x1faff')]:
 target=a.out/(name+'.bin');subprocess.run([a.node,a.converter,'--size','20','--bpp','4','--format','bin','--font',str(font),'-r',ranges,'--no-compress','--no-kerning','-o',str(target)],check=True);shutil.copyfile(license,a.out/(name+'-OFL.txt'));manifest[name]={'source_sha256':hashlib.sha256(font.read_bytes()).hexdigest(),'sha256':hashlib.sha256(target.read_bytes()).hexdigest(),'bytes':target.stat().st_size,'ranges':ranges,'source':'https://github.com/google/fonts/tree/main/ofl/'+('notosanssc' if name=='cjk' else 'notoemoji')}
(a.out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
