#!/usr/bin/env python3
"""Reject local game/derived files, executable payloads and signing material."""
from pathlib import Path
import hashlib,json,subprocess
root=Path(__file__).resolve().parents[1];repo=root.parents[1]
prefix=root.relative_to(repo).as_posix()+'/'
names=subprocess.check_output(['git','-C',str(repo),'ls-files','-z','--',prefix]).decode().split('\0')
allowed={'.c','.h','.inc','.S','.java','.xml','.py','.json','.md','.txt'}
blocked={'romfs_samples.h','nvn_builtin.h','module-manifest.json','local.properties','signing.properties'}
total=0
for name in filter(None,names):
    p=repo/name;relative=p.relative_to(root)
    if p.name in blocked:raise SystemExit(f'Local-only content: {name}')
    if p.suffix not in allowed and p.name not in ('LICENSE','.gitignore','CMakeLists.txt'):raise SystemExit(f'Unexpected payload: {name}')
    if set(part.lower() for part in relative.parts)&{'local','game','romfs','exefs','save','out','build','vendor'}:raise SystemExit(f'Local-only directory: {name}')
    data=p.read_bytes()
    if data.startswith((b'NSO0',b'\x7fELF',b'\xcf\xfa\xed\xfe',b'PK\x03\x04',b'MZ')):raise SystemExit(f'Binary payload: {name}')
    if (b'-----BEGIN '+b'PRIVATE KEY-----') in data:raise SystemExit(f'Private key: {name}')
    total+=len(data)
manifest=json.loads((root/'verified-source.json').read_text())
for entry in manifest['unchanged_source_files']:
    if hashlib.sha256((root/entry['path']).read_bytes()).hexdigest()!=entry['sha256']:
        raise SystemExit(f'Verified baseline source changed: {entry["path"]}')
print(f'PASS source-only payload: {sum(bool(n) for n in names)} files, {total} bytes; no ROM, derived cache, binary or signing files')
