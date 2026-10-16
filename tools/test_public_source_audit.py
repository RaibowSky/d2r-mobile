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
        path = b'F:' + bytes([92]) + b'ning' + bytes([92]) + b'game'
        self.assertIn('workstation path', inspect('notes.md', path))

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

    @staticmethod
    def git(repo, *args):
        subprocess.run(['git', '-C', str(repo), '-c', 'user.name=Audit Test',
                        '-c', 'user.email=audit@example.invalid', *args],
                       check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
