#!/usr/bin/env python3
"""Check the committed port target before cloud build or push."""
from pathlib import Path
import json, subprocess
root=Path(__file__).resolve().parent
repo=root.parents[1]
prefix=root.relative_to(repo).as_posix()+'/'
names=subprocess.check_output(['git','-C',str(repo),'ls-files','-z','--',prefix]).decode().split('\0')
allowed={'.c','.h','.inc','.S','.m','.py','.json','.md','.txt'}
count=0
for name in filter(None,names):
    p=repo/name
    assert p.suffix in allowed or p.name in ('LICENSE','.gitignore'),f'Unexpected committed payload: {name}'
    assert not any(part.lower() in ('game','local','build','vendor','exefs','romfs','save') for part in p.relative_to(root).parts),name
    data=p.read_bytes()
    assert not data.startswith((b'NSO0',b'\x7fELF',b'\xcf\xfa\xed\xfe',b'PK\x03\x04')),name
    assert b'-----BEGIN PRIVATE KEY-----' not in data,name
    count+=1
sources=json.loads((root/'sources.json').read_text())
assert all((root/source).is_file() for source in sources)
print(f'Source-only audit passed: {count} tracked files, {len(sources)} compilation units')
