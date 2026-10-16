#!/usr/bin/env python3
"""Fail before transfer if outgoing commits contain unreviewed local content."""
from pathlib import Path
import subprocess
import sys
from audit_public_source import audit


def main():
    repo = Path(subprocess.check_output(['git', 'rev-parse', '--show-toplevel'], text=True).strip())
    remote = sys.argv[1] if len(sys.argv) > 1 else 'origin'
    for line in sys.stdin:
        local_ref, local_sha, remote_ref, remote_sha = line.split()
        if set(local_sha) == {'0'}:  # Ref deletion carries no source content.
            continue
        # Existing public ancestors remain covered by the historical secret scan;
        # strict metadata rules apply to commits not already on this remote.
        revisions = [local_sha, '--not', f'--remotes={remote}']
        if set(remote_sha) != {'0'}: revisions.append(remote_sha)
        print(f'Auditing outgoing ref {local_ref}', flush=True)
        audit(repo, history=True, ref=local_sha, since=revisions)


if __name__ == '__main__':
    main()
