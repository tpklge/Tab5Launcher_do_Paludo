#!/usr/bin/env python3
"""Generate the launcher's catalog schema 1 from a local app/package directory.
The first file is the entrypoint (.bin or installation package JSON). Files are
published by the user at an HTTPS base URL; this tool does not upload anything.
"""
import argparse,hashlib,json,re
from pathlib import Path
from urllib.parse import urlparse,quote

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(65536),b''): h.update(block)
    return h.hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--entry',type=Path,required=True)
    p.add_argument('--base-url',required=True)
    p.add_argument('--id',required=True);p.add_argument('--name',required=True);p.add_argument('--version',required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();u=urlparse(a.base_url)
    if u.scheme!='https' or not u.hostname or u.username or u.password or u.query or u.fragment: p.error('Use an HTTPS directory URL')
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,47}',a.id):p.error('Invalid id')
    files=[a.entry]
    if a.entry.suffix.lower()=='.json':
        data=json.loads(a.entry.read_text())
        if data.get('schema')!=1 or data.get('chip')!='esp32p4':p.error('Expected launcher installation package schema 1')
        for part in [data['app']]+data.get('data',[]):
            name=part['file']
            if not re.fullmatch(r'[A-Za-z0-9_.-]{1,95}',name) or '..' in name:p.error('Unsafe package filename')
            file=a.entry.parent/name
            if sha(file).lower()!=part['sha256'].lower():p.error('Package hash mismatch: '+name)
            files.append(file)
    if len(files)>5 or len({f.name for f in files})!=len(files):p.error('Too many files or duplicate names')
    parts=[]
    for file in files:
        if not re.fullmatch(r'[A-Za-z0-9_.-]{1,95}',file.name) or '..' in file.name:p.error('Unsafe filename')
        size=file.stat().st_size
        if not 0<size<=32*1024*1024:p.error('File size out of range')
        parts.append(dict(file=file.name,url=a.base_url.rstrip('/')+'/'+quote(file.name),size=size,sha256=sha(file)))
    if len(a.name.encode())>95 or len(a.version.encode())>31:p.error('Name/version too long')
    result=dict(schema=1,items=[dict(id=a.id,name=a.name,version=a.version,files=parts)])
    with a.output.open('x') as f:json.dump(result,f,ensure_ascii=False,indent=2);f.write('\n')
if __name__=='__main__':main()
