"""Exercise the iOS-generated resource through the actual native startup patcher."""
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class IOSStartupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('clang++') or shutil.which('g++')
        if not compiler or not shutil.which('cmake'):
            raise unittest.SkipTest('Native C++ compiler and CMake required')
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.folder = Path(cls.temporary.name)
        cls.tool = cls.folder / 'patch-test'
        tools = ROOT / 'lib/ModernGekko/tools'
        subprocess.run([compiler, '-std=c++20', '-I', str(tools),
                        str(ROOT / 'tests/ios_startup_patch_main.cpp'),
                        str(tools / 'dol_patch.cpp'), '-o', str(cls.tool)], check=True)
        cls.manifest = cls.folder / 'startup-patches.csv'
        subprocess.run(['cmake', '-DREFARE_STARTUP_JSON=' + str(ROOT / 'config/startup-patches.json'),
                        '-DREFARE_STARTUP_CSV=' + str(cls.manifest), '-P',
                        str(ROOT / 'ports/ios/StartupPatches.cmake')], check=True)

    def test_generated_manifest_patches_and_reopens_without_changing_again(self):
        patches = json.loads((ROOT / 'config/startup-patches.json').read_text())['patches']
        base = 0x80003100
        size = max(int(p['address'], 16) for p in patches) - base + 4
        original = bytearray(0x100 + size)
        for offset, value in [(0, 0x100), (0x48, base), (0x90, size)]:
            struct.pack_into('>I', original, offset, value)
        expected = bytearray(original)
        for patch in patches:
            offset = 0x100 + int(patch['address'], 16) - base
            struct.pack_into('>I', original, offset, int(patch['expected'], 16))
            struct.pack_into('>I', expected, offset, int(patch['replacement'], 16))
        dol = self.folder / 'fixture.dol'
        dol.write_bytes(original)
        result = subprocess.run([self.tool, dol, self.manifest], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(dol.read_bytes(), expected)
        second = subprocess.run([self.tool, dol, self.manifest], capture_output=True, text=True)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(second.stdout, 'unchanged')
        self.assertEqual(dol.read_bytes(), expected)
        # Wrong instructions must fail without partially applying any patch.
        wrong = bytearray(original)
        wrong[-1] ^= 0xff
        dol.write_bytes(wrong)
        rejected = subprocess.run([self.tool, dol, self.manifest], capture_output=True, text=True)
        self.assertNotEqual(rejected.returncode, 0)
        self.assertEqual(dol.read_bytes(), wrong)

if __name__ == '__main__':
    unittest.main()
