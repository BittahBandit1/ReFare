"""Public tests: no disc, generated game source, or extracted assets needed."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent

def load(name, file):
    spec = importlib.util.spec_from_file_location(name, ROOT / file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

prepare = load('prepare', 'scripts/prepare-game.py')
ports = load('ports', 'scripts/ports.py')
ciso = load('ciso', 'scripts/ciso-to-iso.py')

class DiscPreparationTests(unittest.TestCase):
    def setUp(self):
        # A fabricated DOL with one text segment, not Crazy Taxi code.
        self.dol = bytearray(0x120)
        struct.pack_into('>I', self.dol, 0, 0x100)
        struct.pack_into('>I', self.dol, 0x48, 0x80004000)
        struct.pack_into('>I', self.dol, 0x90, 0x20)
        struct.pack_into('>I', self.dol, 0x100, 0x12345678)
        self.manifest = {'dol_sha256': hashlib.sha256(self.dol).hexdigest(),
                         'patches': [{'address': '0x80004000', 'expected': '0x12345678',
                                      'replacement': '0x60000000'}]}

    def test_patch_and_original_preservation(self):
        original = bytes(self.dol)
        patched = prepare.patch_dol(self.dol, self.manifest, True)
        self.assertEqual(struct.unpack_from('>I', patched, 0x100)[0], 0x60000000)
        self.assertEqual(self.dol, original)
        self.assertEqual(prepare.patch_dol(original, self.manifest, False), original)
        self.assertEqual(patched[:0x100], original[:0x100])
        self.assertEqual(patched[0x104:], original[0x104:])

    def test_rejects_wrong_revision_and_instruction(self):
        with self.assertRaisesRegex(ValueError, 'revision'):
            prepare.patch_dol(self.dol + b'x', self.manifest, True)
        self.manifest['patches'][0]['expected'] = '0x00000000'
        with self.assertRaisesRegex(ValueError, 'instruction'):
            prepare.patch_dol(self.dol, self.manifest, True)

    def test_rejects_patch_outside_segment(self):
        self.manifest['patches'][0]['address'] = '0x80005000'
        with self.assertRaisesRegex(ValueError, 'outside'):
            prepare.patch_dol(self.dol, self.manifest, True)

    def test_invalid_ciso_does_not_create_output(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory)/'bad.ciso', Path(directory)/'disc.iso'
            source.write_bytes(b'CISO')
            with self.assertRaises(ValueError):
                ciso.convert(source, output)
            self.assertFalse(output.exists())

class NativeTargetTests(unittest.TestCase):
    def test_supported_host_and_mismatched_target(self):
        from unittest.mock import patch
        with patch.object(ports.platform, 'system', return_value='Windows'), \
             patch.object(ports.platform, 'machine', return_value='AMD64'):
            self.assertEqual(ports.host_target(), 'windows-x86_64')
            with self.assertRaisesRegex(ValueError, 'native host'):
                ports.configure('linux-arm64', False)
        with patch.object(ports.platform, 'system', return_value='Linux'), \
             patch.object(ports.platform, 'machine', return_value='aarch64'):
            self.assertEqual(ports.host_target(), 'linux-arm64')

if __name__ == '__main__':
    unittest.main()
