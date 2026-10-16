#!/usr/bin/env python3
"""Opt in this clone to publication checks without replacing existing hooks."""
from pathlib import Path
import subprocess
import sys

repo = Path(__file__).resolve().parents[1]
def git(*args):
    return subprocess.run(['git', '-C', str(repo), *args], text=True, capture_output=True)
configured = git('config', '--get', 'core.hooksPath')
if configured.returncode not in (0, 1):
    raise SystemExit(configured.stderr)
if configured.returncode == 0 and configured.stdout.strip() != '.githooks':
    raise SystemExit('Existing core.hooksPath preserved; integrate pre_push_audit.py with that hook manually')
hook_path = Path(git('rev-parse', '--git-path', 'hooks/pre-push').stdout.strip())
if not hook_path.is_absolute(): hook_path = repo / hook_path
if configured.returncode == 1 and hook_path.exists():
    raise SystemExit('Existing pre-push hook preserved; integrate pre_push_audit.py manually')
for args in [('config', '--local', 'rhythm.auditPython', sys.executable),
             ('config', '--local', 'core.hooksPath', '.githooks')]:
    result = git(*args)
    if result.returncode: raise SystemExit(result.stderr)
print('Enabled this clone: outgoing commit audit runs before git push transfers data')
