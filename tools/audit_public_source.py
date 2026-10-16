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
BLOCKED_DIRS = {'game', 'romfs', 'exefs', 'saves', 'local'}
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
LOCAL_PATH = re.compile(rb'(?i)(?:[A-Z]:\\Users\\(?!YOURNAME\b|you\b|USER\b)[^\\\s`"<>]+|F:\\ning(?:\\|\b))')


def git(repo, *args, data=None):
    return subprocess.check_output(['git', '-C', str(repo), *args], input=data)


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
        if LOCAL_PATH.search(data):
            issues.append('workstation path')
    for label, pattern in PATTERNS.items():
        if pattern.search(data):
            issues.append(label)
    return issues


def audit(repo, history=False):
    issues = []
    names = git(repo, 'ls-files', '-z').decode().split('\0')
    count = 0
    for name in filter(None, names):
        p = repo / name
        if not p.is_file() or p.is_symlink():
            issues.append((name, 'missing or symbolic source file'))
            continue
        count += 1
        issues.extend((name, reason) for reason in inspect(name, p.read_bytes()))
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
            if LOCAL_PATH.search(data):
                path_metadata += 1
            for path in paths[oid]:
                issues.extend((path + ' @' + oid[:10], reason) for reason in inspect(path, data, historical=True))
    for path, reason in sorted(set(issues)):
        print(f'FAIL {reason}: {path}')
    if issues:
        raise SystemExit(1)
    print(f'PASS: {count} tracked files; {historical_count} historical blobs; no blocked payloads or credential patterns detected')
    if path_metadata:
        print(f'NOTE: {path_metadata} historical blobs contain workstation-path metadata; no values printed and no history rewritten')
    return count


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--history', action='store_true')
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    audit(args.repo.resolve(), args.history)
