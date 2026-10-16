"""Publication regression checks with synthetic data; never use real credentials."""
import contextlib
import io
import subprocess
import tempfile
import unittest
from pathlib import Path
from audit_public_source import audit, inspect


class PublicationAuditTests(unittest.TestCase):
    def test_hidden_binary_in_source(self):
        self.assertIn('executable or archive payload', inspect('host.c', b'NSO0' + bytes(64)))

    def test_private_key_versus_detector_literal(self):
        header = b'-----BEGIN ' + b'PRIVATE KEY-----'
        self.assertIn('private key', inspect('host.c', header + b'\n' + b'A' * 64))
        self.assertNotIn('private key', inspect('detector.py', b'pattern = "' + header + b'"'))

    def test_unreviewed_media(self):
        self.assertIn('unreviewed media', inspect('device.png', b'image'))

    def test_current_workstation_path(self):
        for drive in (b'C', b'Q', b'Z'):
            for sep in (bytes([92]), b'/', bytes([92, 92])):
                path = drive + b':' + sep + b'example-workspace' + sep + b'file'
                self.assertIn('workstation path', inspect('notes.md', path))
        for prefix in (b'/home/', b'/Users/', b'/mnt/q/'):
            self.assertIn('workstation path', inspect('notes.md', prefix + b'example-workspace/file'))

    def test_templates_and_urls_are_not_local_paths(self):
        for data in (b'https://example.invalid/docs', b'Q:/<workspace>/file', b'${WORKSPACE}/file', b'rom:/data/file'):
            self.assertNotIn('workstation path', inspect('notes.md', data))

    def test_device_serial_and_templates(self):
        serial = b'EXAMPLE' + b'123456789'
        self.assertIn('device identifier', inspect('notes.md', b'--serial ' + serial))
        self.assertIn('device identifier', inspect('notes.md', b'DM_IOS_UDID=' + serial))
        self.assertIn('device identifier', inspect('notes.json', b'{"serial": "' + serial + b'"}'))
        for data in (b'--serial "$ANDROID_SERIAL"', b'--serial <device>', b'DM_IOS_UDID=00000000-0000000000000000'):
            self.assertNotIn('device identifier', inspect('notes.md', data))

    def test_index_and_commit_scan_do_not_trust_clean_worktree(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            self.git(repo, 'init', '-q')
            p = repo / 'notes.md'
            p.write_text('gh' + 'p_' + 'A' * 36)
            self.git(repo, 'add', 'notes.md')
            p.write_text('clean working copy')
            with contextlib.redirect_stdout(io.StringIO()):
                audit(repo)
                with self.assertRaises(SystemExit):
                    audit(repo, index=True)
                self.git(repo, 'commit', '-qm', 'synthetic fixture')
                with self.assertRaises(SystemExit):
                    audit(repo, ref='HEAD')

    def test_tracked_ignored_key_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            self.git(repo, 'init', '-q')
            (repo / '.gitignore').write_text('*.keys\n')
            (repo / 'test.keys').write_text('synthetic test data')
            self.git(repo, 'add', '-f', 'test.keys')
            with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(SystemExit):
                audit(repo)

    def test_deleted_historical_credential_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            self.git(repo, 'init', '-q')
            (repo / 'notes.txt').write_text('gh' + 'p_' + 'A' * 36)
            self.git(repo, 'add', 'notes.txt')
            self.git(repo, 'commit', '-qm', 'synthetic fixture')
            self.git(repo, 'rm', '-q', 'notes.txt')
            self.git(repo, 'commit', '-qm', 'remove fixture')
            with contextlib.redirect_stdout(io.StringIO()):
                audit(repo)
                with self.assertRaises(SystemExit):
                    audit(repo, history=True)

    def test_deleted_metadata_in_outgoing_commit_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            self.git(repo, 'init', '-q')
            p = repo / 'notes.md'; p.write_text('baseline')
            self.git(repo, 'add', 'notes.md'); self.git(repo, 'commit', '-qm', 'baseline')
            baseline = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD']).decode().strip()
            p.write_text('Q:' + chr(47) + 'example-workspace/file')
            self.git(repo, 'add', 'notes.md'); self.git(repo, 'commit', '-qm', 'private metadata')
            p.write_text('clean')
            self.git(repo, 'add', 'notes.md'); self.git(repo, 'commit', '-qm', 'clean tip')
            with contextlib.redirect_stdout(io.StringIO()):
                audit(repo, ref='HEAD')
                with self.assertRaises(SystemExit):
                    audit(repo, ref='HEAD', since=baseline + '..HEAD')

    @staticmethod
    def git(repo, *args):
        subprocess.run(['git', '-C', str(repo), '-c', 'user.name=Audit Test',
                        '-c', 'user.email=audit@example.invalid', *args],
                       check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
