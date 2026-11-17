"""Exercise on-device table generation and IPA export with synthetic inputs."""
from pathlib import Path
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
import hashlib

ROOT = Path(__file__).resolve().parents[1]

class IOSBuilderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('swiftc'): raise unittest.SkipTest('Swift compiler required')
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.folder = Path(cls.temporary.name)
        cls.binary = cls.folder/'builder-test'
        stub = cls.folder/'native-stubs.o'
        subprocess.run(['clang', '-c', str(ROOT/'tests/ios_builder_stubs.c'), '-o', str(stub)], check=True)
        subprocess.run(['swiftc', '-O', '-module-cache-path', str(cls.folder/'swift-cache'),
                        '-import-objc-header', str(ROOT/'ports/ios/BuilderBridge.h'), str(stub),
                        str(ROOT/'ports/ios/GameBuilder.swift'),
                        str(ROOT/'ports/ios/ModuleTables.swift'), str(ROOT/'ports/ios/IPAArchive.swift'),
                        str(ROOT/'tests/ios_builder_main.swift'), '-o', str(cls.binary)], check=True)

    def invoke(self, *args, **kwargs):
        return subprocess.run([str(self.binary), *map(str, args)], check=True, **kwargs)

    def test_tables_match_reference(self):
        for compact in (False, True):
            generated = self.folder/str(compact)
            generated.mkdir()
            header = 'void func_80003100(CPUState* ctx);\nvoid func_80003108(CPUState* ctx);\n'
            header += ('u32 offset = address - 0x80003100u;\nif (offset < 0x10u)' if compact
                       else 'address >= 0x80003100u && address < 0x80003110u')
            (generated/'generated.h').write_text(header)
            (generated/'generated_smc.txt').write_text('0x80003104-0x80003108\n')
            dol = bytearray(0x110)
            for offset, value in ((0,0x100), (0x48,0x80003100), (0x90,0x10)):
                struct.pack_into('>I', dol, offset, value)
            dol[0x100:] = bytes(range(16))
            (generated/'main.dol').write_bytes(dol)
            output, reference = generated/'swift.inc', generated/'python.inc'
            self.invoke('tables', generated, output)
            subprocess.run([sys.executable, str(ROOT/'lib/ModernGekko/vendor/dolphin/module-template/gen_module_tables.py'),
                            str(generated/'generated.h'), str(generated/'generated_smc.txt'), str(generated/'main.dol'), str(reference)], check=True)
            normalize = lambda p: re.sub(r'\s+', '', re.sub(r'//[^\n]*', '', p.read_text()))
            self.assertEqual(normalize(output), normalize(reference))
            # Truncation must fail rather than publishing incorrect code hashes.
            (generated/'main.dol').write_bytes(dol[:0x108])
            result = subprocess.run([str(self.binary), 'tables', str(generated), str(output)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)

    def test_ipa_roundtrip_permissions_and_cancellation(self):
        payload = self.folder/'Payload'
        app = payload/'ReFare.app'
        app.mkdir(parents=True)
        binary = app/'ReFare'
        binary.write_bytes(bytes(range(256)) * 8192)
        binary.chmod(0o755)
        (app/'space ü.txt').write_text('personal export fixture')
        output = self.folder/'personal.ipa'
        self.invoke('zip', payload, output)
        with zipfile.ZipFile(output) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(archive.read('Payload/ReFare.app/ReFare'), binary.read_bytes())
            self.assertEqual(archive.getinfo('Payload/ReFare.app/ReFare').external_attr >> 16 & 0o777, 0o755)
            self.assertEqual(archive.read('Payload/ReFare.app/space ü.txt').decode(), 'personal export fixture')
        original = output.read_bytes()
        self.invoke('cancel', payload, output)
        self.assertEqual(output.read_bytes(), original)
        self.assertFalse(output.with_suffix('.ipa.partial').exists())

    def test_symlink_rejected(self):
        payload = self.folder/'symlinks'
        payload.mkdir()
        (payload/'linked').symlink_to('/etc/hosts')
        output = self.folder/'bad.ipa'
        result = subprocess.run([str(self.binary), 'zip', str(payload), str(output)], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(output.exists())
        self.assertFalse(output.with_suffix('.ipa.partial').exists())

    def test_template_data_roundtrip_and_validation(self):
        sys.path.insert(0, str(ROOT/'scripts'))
        from ios_builder import write_template
        app = self.folder/'template.app'
        app.mkdir()
        binary = app/'ReFare'
        binary.write_bytes(b'generic runtime fixture')
        binary.chmod(0o755)
        template = self.folder/'template.rfarchive'
        write_template(app, template)
        destination = self.folder/'extracted.app'
        self.invoke('extract', template, destination)
        self.assertEqual((destination/'ReFare').read_bytes(), binary.read_bytes())
        self.assertEqual((destination/'ReFare').stat().st_mode & 0o777, 0o755)
        original = template.read_bytes()
        for name, data in [('truncated', original[:-1]), ('corrupt', original[:-1] + b'x'),
                           ('trailing', original + b'x')]:
            template.write_bytes(data)
            destination = self.folder/(name + '.app')
            result = subprocess.run([str(self.binary), 'extract', str(template), str(destination)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(destination.exists())
        name = b'../escaped'
        template.write_bytes(b'RFTPL001' + struct.pack('<IHIQ', 1, len(name), 0o644, 1) + hashlib.sha256(b'x').digest() + name + b'x')
        result = subprocess.run([str(self.binary), 'extract', str(template), str(self.folder/'unsafe.app')], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.folder/'escaped').exists())

    def test_cached_export_and_corruption_rejection(self):
        sys.path.insert(0, str(ROOT/'scripts'))
        from ios_builder import write_template
        root, assets, output = [self.folder/name for name in ('profile', 'assets', 'output')]
        game = root/'GCTE51/sys'
        game.mkdir(parents=True)
        dol = b'synthetic personal DOL fixture'
        (game/'main.dol').write_bytes(dol)
        assets.mkdir()
        tool_id = '0' * 64
        (assets/'compiler-id.txt').write_text(tool_id)
        template = self.folder/'cache-template.app'
        template.mkdir()
        (template/'ReFare').write_bytes(b'generic runtime')
        write_template(template, assets/'GameplayTemplate.rfarchive')
        cache = root/'BuilderCache'/(hashlib.sha256(dol).hexdigest() + '-' + tool_id)
        framework = cache/'ReFareGame.framework'
        framework.mkdir(parents=True)
        binary = framework/'ReFareGame'
        binary.write_bytes(b'synthetic compiled module fixture')
        (cache/'module.sha256').write_text(hashlib.sha256(binary.read_bytes()).hexdigest())
        self.invoke('export', assets, root, output)
        ipa = output/'ReFare-Personal-iOS15.ipa'
        original = ipa.read_bytes()
        with zipfile.ZipFile(ipa) as archive:
            self.assertEqual(archive.read('Payload/ReFare.app/Frameworks/ReFareGame.framework/ReFareGame'), binary.read_bytes())
            self.assertEqual(archive.read('Payload/ReFare.app/PersonalGame/sys/main.dol'), dol)
        binary.write_bytes(b'corrupted module')
        result = subprocess.run([str(self.binary), 'export', str(assets), str(root), str(output)], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(ipa.read_bytes(), original)
        self.assertEqual(list((root/'cache/ReFareBuilder').iterdir()), [])

if __name__ == '__main__': unittest.main()
