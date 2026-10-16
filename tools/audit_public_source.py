#!/usr/bin/env python3
"""Scan tracked source and optional reachable history without printing secrets.

This is a publication safeguard, not a complete secret/IP classification engine.
Historical workstation paths are reported as metadata warnings; current paths fail.
"""
import argparse
import re
import subprocess
from pathlib import Path

BLOCKED_SUFFIXES = {
    '.xci', '.nsp', '.nca', '.nso', '.romfs', '.bin', '.spv', '.metallib',
    '.vkmeta', '.ipa', '.apk', '.aab', '.apks', '.idsig', '.keys', '.key',
    '.pem', '.cer', '.crt', '.p12', '.pfx', '.jks', '.keystore',
    '.mobileprovision', '.provisionprofile', '.ttf', '.ttc', '.otf', '.log',
    '.dmp', '.exe', '.dll', '.so', '.dylib', '.a', '.o', '.zip', '.7z', '.rar',
}
BLOCKED_NAMES = {
    'romfs_samples.h', 'module-manifest.json', 'local.properties',
    'signing.properties', 'id_rsa', 'id_ed25519', '.env',
}
BLOCKED_DIRS = {'game', 'romfs', 'exefs', 'saves', 'local', 'build', 'out'}
# Existing upstream UI images only; do not admit additional game/device images.
MEDIA = {
    'dm/ios/Assets.xcassets/AppIcon.appiconset/AppIcon1024.png',
    'dm/ios/NativeHUD/touch/close.png',
    'dm/ios/Sources/TouchArt/dm_touch_joy.png',
}
MAGIC = (b'NSO0', b'\x7fELF', b'MZ', b'PK\x03\x04', b'\xcf\xfa\xed\xfe', b'\x03\x02\x23\x07')
PATTERNS = {
    'private key': re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA |ENCRYPTED )?PRIVATE KEY-----\s+[A-Za-z0-9+/=\r\n]{32,}'),
    'GitHub token': re.compile(rb'\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,})\b'),
    'AWS access key': re.compile(rb'\b(?:AKIA|ASIA)[A-Z0-9]{16}\b'),
    'Google API key': re.compile(rb'\bAIza[0-9A-Za-z_-]{35}\b'),
    'Slack token': re.compile(rb'\bxox[baprs]-[0-9A-Za-z-]{20,}\b'),
    'credential in URL': re.compile(rb'https?://[^\s/:<>]+:[^\s/@<>]+@'),
    'literal credential': re.compile(rb'''(?m)^\s*(?:export\s+)?(?:[A-Z_]*(?:PASSWORD|SECRET|TOKEN|PRIVATE_KEY)|storePassword|keyPassword)\s*[:=]\s*["'][A-Za-z0-9+/=_-]{12,}["']'''),
}
# Match drive-absolute paths, including JSON/C escaped backslashes. Template
# roots beginning with <, $, %, or { are intentionally permitted.
LOCAL_PATH = re.compile(rb'''(?ix)(?<![a-z0-9_])(?:
    [a-z]:[\\/]+[a-z0-9_.-]+(?:[\\/][^\s`"'<>]*)?
    |/(?:Users|home)/[a-z0-9_.-]+(?:/[^\s`"'<>]*)?
    |/mnt/[a-z]/[a-z0-9_.-]+(?:/[^\s`"'<>]*)?
)''')
DEVICE_ID = re.compile(rb'''(?ix)
    (?:--(?:serial|udid)(?:\s+|=)|\b(?:DM_IOS_UDID|ANDROID_SERIAL|DEVICE_SERIAL)\s*=\s*|["'](?:serial|udid)["']\s*:\s*)
    ["']?((?=[a-z0-9-]{8,64}\b)(?=[a-z0-9-]*[0-9])[a-z0-9]+(?:-[a-z0-9]+)?)
''')


def metadata_issues(data):
    reasons = []
    if LOCAL_PATH.search(data):
        reasons.append('workstation path')
    if any(set(m.group(1)) - {ord('0'), ord('-')} for m in DEVICE_ID.finditer(data)):
        reasons.append('device identifier')
    return reasons


def git(repo, *args, data=None):
    return subprocess.check_output(['git', '-C', str(repo), *args], input=data)


def tree(repo, ref):
    result = []
    for entry in git(repo, 'ls-tree', '-r', '-z', ref).split(b'\0'):
        if entry:
            meta, name = entry.split(b'\t', 1)
            mode, kind, oid = meta.decode().split()
            result.append((mode, kind, oid, name.decode()))
    return result


def blobs(repo, ids):
    raw = git(repo, 'cat-file', '--batch', data=''.join(oid + '\n' for oid in ids).encode())
    result = {}; offset = 0
    while offset < len(raw):
        end = raw.index(b'\n', offset)
        oid, kind, size = raw[offset:end].decode().split(); size = int(size)
        if kind != 'blob':
            raise ValueError('Expected Git blob')
        result[oid] = raw[end + 1:end + 1 + size]; offset = end + 2 + size
    return result


def inspect(path, data, historical=False):
    issues = []
    parts = Path(path).parts
    name = Path(path).name.lower()
    if (Path(path).suffix.lower() in BLOCKED_SUFFIXES or name in BLOCKED_NAMES
            or name.startswith('.env.') or any(p.lower() in BLOCKED_DIRS for p in parts)):
        issues.append('local-only filename')
    if len(data) > 2_000_000:
        issues.append('oversized source payload')
    if data.startswith(MAGIC):
        issues.append('executable or archive payload')
    if not historical:
        if Path(path).suffix.lower() in {'.png', '.jpg', '.jpeg', '.webp', '.mp4', '.wav', '.ogg', '.mp3'} and path not in MEDIA:
            issues.append('unreviewed media')
        issues.extend(metadata_issues(data))
    for label, pattern in PATTERNS.items():
        if pattern.search(data):
            issues.append(label)
    return issues


def audit(repo, history=False, ref=None, index=False, since=None):
    issues = []
    if ref and index:
        raise ValueError('Choose either --ref or --index')
    snapshot = {}
    if ref:
        entries = tree(repo, ref)
        snapshot = blobs(repo, {oid for mode, kind, oid, name in entries if kind == 'blob'})
        names = [name for mode, kind, oid, name in entries]
        entry_by_name = {name: (mode, kind, oid) for mode, kind, oid, name in entries}
    else:
        names = git(repo, 'ls-files', '-z').decode().split('\0')
        if index:
            index_entries = []
            for entry in git(repo, 'ls-files', '--stage', '-z').split(b'\0'):
                if entry:
                    meta, name = entry.split(b'\t', 1)
                    mode, oid, stage = meta.decode().split()
                    index_entries.append((name.decode(), oid))
                    if mode in ('120000', '160000') or stage != '0':
                        issues.append((name.decode(), 'unreviewed link, submodule or merge conflict'))
            snapshot = blobs(repo, {oid for name, oid in index_entries})
            index_by_name = dict(index_entries)
    count = 0
    for name in filter(None, names):
        p = repo / name
        if not (ref or index) and (not p.is_file() or p.is_symlink()):
            issues.append((name, 'missing or symbolic source file'))
            continue
        count += 1
        if ref:
            mode, kind, oid = entry_by_name[name]
            if kind != 'blob' or mode == '120000':
                issues.append((name, 'unreviewed link or submodule')); continue
            data = snapshot[oid]
        else:
            data = snapshot[index_by_name[name]] if index else p.read_bytes()
        issues.extend((name, reason) for reason in inspect(name, data))
    if since:
        # Check every newly pushed snapshot, including content deleted at the tip.
        checked = set()
        revisions = [since] if isinstance(since, str) else since
        for commit in git(repo, 'rev-list', *revisions).decode().splitlines():
            entries = tree(repo, commit)
            content = blobs(repo, {oid for mode, kind, oid, name in entries if kind == 'blob' and oid not in snapshot})
            snapshot.update(content)
            for mode, kind, oid, name in entries:
                if kind != 'blob' or mode == '120000':
                    issues.append((name, 'unreviewed link or submodule'))
                    continue
                if (name, oid) in checked:continue
                checked.add((name, oid))
                issues.extend((name + ' @' + commit[:10], reason)
                              for reason in inspect(name, snapshot[oid]))
    historical_count = 0
    path_metadata = 0
    if history:
        # Enumerate every commit tree as well: one blob may have several filenames.
        paths = {}
        for commit in git(repo, 'rev-list', '--all').decode().splitlines():
            for entry in git(repo, 'ls-tree', '-r', '-z', commit).split(b'\0'):
                if not entry:
                    continue
                meta, path = entry.split(b'\t', 1)
                mode, kind, oid = meta.decode().split()
                if kind == 'blob':
                    paths.setdefault(oid, set()).add(path.decode())
                else:
                    issues.append((path.decode(), 'unreviewed submodule'))
        request = ''.join(oid + '\n' for oid in paths).encode()
        raw = git(repo, 'cat-file', '--batch', data=request)
        offset = 0
        while offset < len(raw):
            end = raw.index(b'\n', offset)
            oid, kind, size = raw[offset:end].decode().split()
            size = int(size)
            data = raw[end + 1:end + 1 + size]
            offset = end + 2 + size
            historical_count += 1
            if metadata_issues(data):
                path_metadata += 1
            for path in paths[oid]:
                issues.extend((path + ' @' + oid[:10], reason) for reason in inspect(path, data, historical=True))
    for path, reason in sorted(set(issues)):
        print(f'FAIL {reason}: {path}')
    if issues:
        raise SystemExit(1)
    print(f'PASS: {count} tracked files; {historical_count} historical blobs; no blocked payloads or credential patterns detected')
    if path_metadata:
        print(f'NOTE: {path_metadata} historical blobs contain path/device metadata; no values printed and no history rewritten')
    return count


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--history', action='store_true')
    parser.add_argument('--ref', help='Scan committed content rather than the worktree')
    parser.add_argument('--index', action='store_true', help='Scan staged content rather than the worktree')
    parser.add_argument('--since', help='Also scan every commit in this revision range')
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    audit(args.repo.resolve(), args.history, args.ref, args.index, args.since)
