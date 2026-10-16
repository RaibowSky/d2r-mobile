import tempfile
import unittest
from pathlib import Path
import xml.etree.ElementTree as ET
from package_unsigned import ANDROID,prepare_manifest,verify_packaged_manifest

class ManifestTests(unittest.TestCase):
    def test_build_modes_keep_identity_and_disable_debug_by_default(self):
        source=Path(__file__).resolve().parents[1]/'dm/android/AndroidManifest.xml'
        self.assertEqual(ET.parse(source).getroot().find('application').get(ANDROID+'debuggable'),'false')
        with tempfile.TemporaryDirectory() as tmp:
            for debug in (False,True):
                p=Path(tmp)/'manifest.xml';prepare_manifest(source,p,debug)
                root=ET.parse(p).getroot();app=root.find('application')
                self.assertEqual(root.get('package'),'org.d2r.binaryprobe')
                self.assertEqual(app.get(ANDROID+'label'),'節奏天國')
                self.assertEqual(app.get(ANDROID+'allowBackup'),'false')
                self.assertEqual(app.get(ANDROID+'debuggable'),'true' if debug else 'false')

    def test_binary_manifest_mismatch_and_missing_flag_fail(self):
        for output in (b'no flag',b'android:debuggable(0x0101000f)=(type 0x12)0xffffffff'):
            with self.assertRaises(ValueError):
                verify_packaged_manifest('aapt2','test.apk',False,lambda *a:output)
        verify_packaged_manifest('aapt2','test.apk',False,lambda *a:b'android:debuggable(0x0101000f)=(type 0x12)0x0')

if __name__=='__main__':unittest.main()
