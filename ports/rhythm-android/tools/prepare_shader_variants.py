"""Prepare the previously observed variants from a version-locked local BNSH.

The index stores complete control and code bytes. Only an exact runtime match
selects a variant; code hashes are also included in each artifact identity.
"""
from pathlib import Path
import json,struct,hashlib
import argparse
parser=argparse.ArgumentParser(description="Prepare known game-version variants from the local BNSH; no shader data ships in Git")
parser.add_argument('--bnsh',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
OUT=args.output.resolve();OUT.mkdir(parents=True,exist_ok=False)
BNSH=args.bnsh.read_bytes()
assert hashlib.sha256(BNSH).hexdigest()=='18b778b0aa0b90106db72e474de7c0c691777e11fbd859ade7563c91280553ff', 'Unsupported BNSH revision'
jobs=[];entries=[]
for index,control_off,code_off,size in ((0,0xaa0,0x15200,0x300),(1,0x1320,0x15000,0x200),(2,0x6908,0x16e00,0x200),(3,0x2a10,0x17000,0x200),(4,0x2a10,0x15800,0x200),(5,0x2a10,0x15a00,0x200),(6,0xb4d0,0x18200,0x300),(7,0xb4d0,0x18500,0x300),(8,0xb4d0,0x1a700,0x300)):
    control=BNSH[control_off:control_off+0x880]
    code=BNSH[code_off:code_off+size]
    assert struct.unpack_from('<I',code)[0]==0x12345678
    sph_stage=(struct.unpack_from('<I',code,0x30)[0]>>10)&15
    assert sph_stage==(1 if index==0 else 5)
    stage=0 if sph_stage==1 else 1
    h=0xcbf29ce484222325^stage
    for byte in control+code:h=((h^byte)*0x100000001b3)&0xffffffffffffffff
    key=f'{h:016x}'
    path=OUT/f'{key}.maxwell'
    path.write_bytes(code[0x30:])
    jobs.append({'key':key,'stage':'vs' if stage==0 else 'ps','code':str(path),
                 'attr':{},'fout':{'0':'float'},'debug':False})
    entries.append({'key':key,'stage':stage,'control':control_off,'code':code_off,
                    'bytes':size,'code_sha256':hashlib.sha256(code).hexdigest()})
(OUT/'jobs.json').write_text(json.dumps(jobs,indent=2),encoding='utf-8')
(OUT/'manifest.json').write_text(json.dumps(entries,indent=2),encoding='utf-8')
with (OUT/'shader_index.bin').open('wb') as f:
    f.write(struct.pack('<3I',0x494b5644,2,len(entries)))
    for e in entries:f.write(e['key'].encode()+struct.pack('<2I',e['stage'],e['bytes'])+BNSH[e['control']:e['control']+0x880]+BNSH[e['code']:e['code']+e['bytes']])
print(json.dumps(entries,indent=2))
