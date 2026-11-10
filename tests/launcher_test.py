"""Public launcher workflow tests; no game data or graphics device required."""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import platform
import struct
import sys
import tempfile
import types
import unittest
import zipfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('launcher_backend', ROOT / 'launcher/launcher_backend.py')
backend = importlib.util.module_from_spec(spec)
spec.loader.exec_module(backend)

class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='refare launcher test ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.support = self.root / 'user data'
        self.support.mkdir()
        self.disc = self.root / 'personal disc.iso'
        self.header = bytearray(0x8000)
        self.header[:6] = b'GCTE51'
        self.header[0x1c:0x20] = bytes.fromhex('c2339f3d')
        self.disc.write_bytes(self.header)
        self.tools = self.root / 'tools'
        self.tools.mkdir()
        self.suffix = '.dll' if os.name == 'nt' else '.dylib' if sys.platform == 'darwin' else '.so'

    def installed(self):
        game = self.support / 'installs/old/prepared'
        (game / 'sys').mkdir(parents=True)
        (game / 'files').mkdir()
        (game / 'sys/main.dol').write_bytes(b'fabricated executable')
        (game / 'files/ct').write_bytes(b'fabricated asset')
        module = game.parent / ('module' + self.suffix)
        module.write_bytes(b'fabricated native module')
        record = dict(version=1, platform=sys.platform, architecture=platform.machine().lower(),
                      game=str(game.relative_to(self.support)), module=str(module.relative_to(self.support)),
                      dol_sha256=hashlib.sha256((game / 'sys/main.dol').read_bytes()).hexdigest())
        (self.support / 'installation.json').write_text(json.dumps(record))
        return game, module

    def fake_prepare(self, source, game, skip):
        # The revision-specific patcher is tested separately. Here we exercise
        # the launcher's real transaction, file ownership and pointer commit.
        (game / 'sys').mkdir(parents=True)
        (game / 'sys/main.dol').write_bytes(b'fabricated prepared executable')
        try:
            (game / 'files').symlink_to(source / 'files', target_is_directory=True)
        except OSError:
            import shutil
            shutil.copytree(source / 'files', game / 'files')

    def fake_run(self, command, support, env):
        if 'extract' in command:
            extracted = Path(command[-1])
            (extracted / 'files').mkdir(parents=True)
            (extracted / 'files/ct').write_bytes(b'fabricated asset')
        elif 'build' in command:
            modules = Path(command[-1])
            published = modules / 'GCTE51/cache' / ('gGCTE51_recomp' + self.suffix)
            published.parent.mkdir(parents=True)
            published.write_bytes(b'fabricated native module')
            duplicate = published.parent / 'module-build' / published.name
            duplicate.parent.mkdir(); duplicate.write_bytes(b'build copy')
            (modules / 'GCTE51/active-module.txt').write_text(str(published))

    def test_disc_checks(self):
        self.assertFalse(backend.inspect_disc(self.disc))
        self.header[:6] = b'WRONG1'; self.disc.write_bytes(self.header)
        with self.assertRaisesRegex(ValueError, 'GCTE51'):
            backend.inspect_disc(self.disc)
        self.header[:6] = b'GCTE51'; self.header[7] = 1; self.disc.write_bytes(self.header)
        with self.assertRaisesRegex(ValueError, 'revision 0'):
            backend.inspect_disc(self.disc)

    def test_truncated_ciso_rejected_before_preparation(self):
        source = self.root / 'bad.ciso'
        header = bytearray(0x8000)
        header[:4] = b'CISO'; struct.pack_into('<I', header, 4, 0x10000); header[8] = 1
        source.write_bytes(header)
        with self.assertRaisesRegex(ValueError, 'truncated'):
            backend.inspect_disc(source)

    def test_marker_validation_and_platform_isolation(self):
        game, module = self.installed()
        self.assertEqual(backend.load_installation(self.support), (game, module))
        (game / 'sys/main.dol').write_bytes(b'changed')
        self.assertIsNone(backend.load_installation(self.support))
        (game / 'sys/main.dol').write_bytes(b'fabricated executable')
        marker = self.support / 'installation.json'
        record = json.loads(marker.read_text())
        record['game'] = str(self.root / 'outside')
        marker.write_text(json.dumps(record))
        self.assertIsNone(backend.load_installation(self.support))
        record['game'] = str(game.relative_to(self.support))
        record['architecture'] = 'a-different-cpu'
        marker.write_text(json.dumps(record))
        self.assertIsNone(backend.load_installation(self.support))

    def test_malformed_marker_returns_to_disc_selection(self):
        for value in ([], None, {'version': 999}, {'version': 1, 'game': 12}):
            (self.support / 'installation.json').write_text(json.dumps(value))
            self.assertIsNone(backend.load_installation(self.support))

    def test_failed_compile_preserves_previous_installation(self):
        old = self.installed()
        marker = (self.support / 'installation.json').read_bytes()
        def fail(command, support, env):
            if 'build' in command:
                raise RuntimeError('Compiler failed')
            self.fake_run(command, support, env)
        with patch.object(backend, 'check_tools', return_value=[]), \
             patch.object(backend, 'run', side_effect=fail), \
             patch.object(backend, 'helper', return_value=types.SimpleNamespace(prepare=self.fake_prepare)), \
             contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(RuntimeError, 'Compiler failed'):
                backend.install(self.disc, self.support, self.tools, 'gcc', {})
        self.assertEqual((self.support / 'installation.json').read_bytes(), marker)
        self.assertEqual(backend.load_installation(self.support), old)
        self.assertEqual([p.name for p in (self.support / 'installs').iterdir()], ['old'])

    def test_successful_install_keeps_saves_and_commits_complete_game(self):
        saves = self.support / 'user/GC/saves'
        saves.mkdir(parents=True)
        (saves / 'personal-save').write_bytes(b'my save')
        original = self.disc.read_bytes()
        output = io.StringIO()
        with patch.object(backend, 'check_tools', return_value=[]), \
             patch.object(backend, 'run', side_effect=self.fake_run), \
             patch.object(backend, 'helper', return_value=types.SimpleNamespace(prepare=self.fake_prepare)), \
             contextlib.redirect_stdout(output):
            backend.install(self.disc, self.support, self.tools, 'gcc', {})
        installed = backend.load_installation(self.support)
        self.assertIsNotNone(installed)
        game, module = installed
        self.assertTrue(module.is_file())
        self.assertFalse((game.parent / 'extracted').exists())
        self.assertFalse((game / 'files').is_symlink())
        self.assertEqual((saves / 'personal-save').read_bytes(), b'my save')
        self.assertEqual(self.disc.read_bytes(), original)
        self.assertEqual(json.loads(output.getvalue().splitlines()[-1])['event'], 'ready')

    def test_missing_tools_do_not_replace_game(self):
        old = self.installed()
        output = io.StringIO()
        with patch.object(backend, 'check_tools', return_value=['cmake']), contextlib.redirect_stdout(output):
            backend.install(self.disc, self.support, self.tools, 'gcc', {})
        event = json.loads(output.getvalue())
        self.assertEqual(event['event'], 'missing_tools')
        self.assertEqual(event['missing'], ['cmake'])
        self.assertEqual(backend.load_installation(self.support), old)

    def test_play_uses_runner_without_a_compiler(self):
        game, module = self.installed()
        args = ['launcher_backend.py', 'play', '--support-dir', str(self.support),
                '--tools-dir', str(self.tools), '--source-dir', str(ROOT / 'lib/ModernGekko'), '--compiler', 'gcc']
        with patch.object(sys, 'argv', args), patch.object(backend, 'run') as run, \
             patch.object(backend, 'compiler_environment', side_effect=AssertionError('Compiler accessed during Play')), \
             contextlib.redirect_stdout(io.StringIO()):
            backend.main()
        command = run.call_args.args[0]
        self.assertIn('moderngekko-run', Path(command[0]).stem)
        self.assertIn(game.resolve(), command); self.assertIn(module.resolve(), command)
        self.assertNotIn('build', command)

    def test_subprocess_arguments_are_not_shell_commands(self):
        output = io.StringIO()
        literal = 'spaces & $(not-a-command); "quoted"'
        with contextlib.redirect_stdout(output):
            backend.run([sys.executable, '-c', 'import sys; print(sys.argv[1])', literal], self.support, os.environ.copy())
        self.assertEqual(json.loads(output.getvalue())['text'], literal)

    def test_operation_lock_blocks_another_window(self):
        with backend.operation_lock(self.support):
            with self.assertRaisesRegex(RuntimeError, 'Another ReFare'):
                with backend.operation_lock(self.support):
                    self.fail('Second operation acquired the same library')
        with backend.operation_lock(self.support):
            pass

    def native_driver(self, android=False, wrong_cpu=False):
        directory = self.root / 'my graphics driver'
        directory.mkdir(exist_ok=True)
        library = directory / 'libvulkan_test.so'
        data = bytearray(1024)
        data[:7] = b'\x7fELF\x02\x01\x01'
        machine = 183 if platform.machine().lower() in ('aarch64', 'arm64') else 62
        if wrong_cpu: machine = 62 if machine == 183 else 183
        struct.pack_into('<HH', data, 16, 3, machine)
        struct.pack_into('<Q', data, 32, 64)
        struct.pack_into('<HH', data, 54, 56, 2)
        struct.pack_into('<IIQQQQQQ', data, 64, 1, 4, 0, 0, 0, 1024, 1024, 8)
        dependencies = ['libc.so', 'liblog.so'] if android else ['libc.so.6']
        dynamic = [(1, 0)]
        strings = dependencies[0].encode() + b'\0'
        for name in dependencies[1:]:
            dynamic.append((1, len(strings))); strings += name.encode() + b'\0'
        dynamic += [(5, 512), (10, len(strings)), (0, 0)]
        struct.pack_into('<IIQQQQQQ', data, 120, 2, 4, 176, 176, 0, len(dynamic)*16, len(dynamic)*16, 8)
        for i, (tag, value) in enumerate(dynamic): struct.pack_into('<QQ', data, 176+i*16, tag, value)
        data[512:512+len(strings)] = strings
        library.write_bytes(data)
        manifest = directory / 'turnip-test.json'
        manifest.write_text(json.dumps({'file_format_version': '1.0.0', 'ICD': {
            'api_version': '1.3.0', 'library_path': './libvulkan_test.so'}}))
        return manifest, library

    def test_linux_driver_path_is_resolved_without_modifying_source(self):
        manifest, library = self.native_driver()
        original = manifest.read_bytes()
        info = backend.validate_driver(manifest)
        self.assertEqual(info['ICD']['library_path'], str(library.resolve()))
        self.assertEqual(manifest.read_bytes(), original)

    def test_android_and_wrong_architecture_drivers_are_rejected(self):
        manifest, _ = self.native_driver(android=True)
        with self.assertRaisesRegex(ValueError, 'Android/Bionic'):
            backend.validate_driver(manifest)
        manifest, _ = self.native_driver(wrong_cpu=True)
        with self.assertRaisesRegex(ValueError, 'different CPU'):
            backend.validate_driver(manifest)
        with self.assertRaisesRegex(ValueError, 'Android driver ZIPs'):
            backend.validate_driver(self.root / 'TurnipZ.zip')

    def test_custom_driver_is_scoped_to_child_environment(self):
        manifest, library = self.native_driver()
        (self.support/'vulkan-driver.json').write_text(json.dumps({'manifest':str(manifest)}))
        parent = {'KEEP':'preserved', 'VK_DRIVER_FILES':'parent-choice'}
        with contextlib.redirect_stdout(io.StringIO()):
            child = backend.driver_environment(self.support, parent)
        self.assertEqual(parent['VK_DRIVER_FILES'], 'parent-choice')
        self.assertEqual(child['KEEP'], 'preserved')
        self.assertEqual(child['VK_DRIVER_FILES'], child['VK_ICD_FILENAMES'])
        normalized = json.loads(Path(child['VK_DRIVER_FILES']).read_text())
        self.assertEqual(normalized['ICD']['library_path'], str(library.resolve()))
        (self.support/'vulkan-driver.json').write_text(json.dumps({'manifest':None}))
        with contextlib.redirect_stdout(io.StringIO()):
            restored = backend.driver_environment(self.support, parent)
        self.assertEqual(restored, parent)

    def test_invalid_driver_selection_preserves_previous_choice(self):
        manifest, _ = self.native_driver()
        original = json.dumps({'manifest':str(manifest)})
        (self.support/'vulkan-driver.json').write_text(original)
        with zipfile.ZipFile(self.root/'Android-driver.zip','w') as archive:
            archive.writestr('meta.json',json.dumps({'minApi':30,'libraryName':'libvulkan_freedreno.so'}))
        args = ['launcher_backend.py', 'driver', '--support-dir',str(self.support),
                '--tools-dir',str(self.tools),'--source-dir',str(ROOT/'lib/ModernGekko'),
                '--compiler','gcc','--driver-json',str(self.root/'Android-driver.zip')]
        with patch.object(sys,'argv',args), patch.object(sys,'platform','linux'):
            with self.assertRaisesRegex(ValueError,'Android/AdrenoTools'):
                backend.main()
        self.assertEqual((self.support/'vulkan-driver.json').read_text(), original)

    def test_linux_driver_zip_is_imported_and_traversal_is_rejected(self):
        manifest, library = self.native_driver()
        source = self.root/'linux-driver.zip'
        with zipfile.ZipFile(source,'w') as archive:
            archive.write(manifest,manifest.name); archive.write(library,library.name)
        imported = backend.import_driver(self.support,source)
        self.assertTrue(imported.is_file())
        self.assertIn(self.support.resolve(),imported.parents)
        self.assertNotEqual(imported,manifest)
        with zipfile.ZipFile(source,'w') as archive:
            archive.writestr('../outside.txt','invalid')
        with self.assertRaisesRegex(ValueError,'invalid path'):
            backend.import_driver(self.support,source)
        self.assertFalse((self.root/'outside.txt').exists())

    @unittest.skipIf(os.name == 'nt', 'Linux personal-module workflow')
    def test_personal_module_install_needs_no_developer_tools(self):
        binary = self.root/'personal.so'; binary.write_bytes(b'personal native module')
        expected = hashlib.sha256(b'fabricated prepared executable').hexdigest()
        for tool in ('dolrecomp','moderngekko-run'):
            backend.executable(self.tools,tool).write_bytes(b'test executable')
        calls = []
        def run(command,support,env):
            calls.append(command); self.fake_run(command,support,env)
        with patch.object(sys,'platform','linux'), \
             patch.object(backend,'bundled_module',return_value=(binary,expected)), \
             patch.object(backend,'run',side_effect=run), \
             patch.object(backend,'helper',return_value=types.SimpleNamespace(prepare=self.fake_prepare)), \
             contextlib.redirect_stdout(io.StringIO()):
            backend.install(self.disc,self.support,self.tools,'gcc',{'PATH':''})
            installed = backend.load_installation(self.support)
        self.assertIsNotNone(installed)
        self.assertEqual(installed[1].read_bytes(),binary.read_bytes())
        self.assertTrue(all('build' not in command for command in calls))

    @unittest.skipIf(os.name == 'nt', 'Linux handheld preset')
    def test_handheld_preset_is_applied_once_and_preserves_user_changes(self):
        binary=self.root/'native.so';binary.write_bytes(b'personal module')
        resource=self.root/'resources';(resource/'config').mkdir(parents=True)
        (resource/'config/GFX.ini').write_text('[Settings]\nAspectRatio=3\n')
        preset='[Video]\nresolution=640x528\nfullscreen=true\n'
        (resource/'config/frontend.ini').write_text(preset)
        expected=hashlib.sha256(b'fabricated prepared executable').hexdigest()
        with patch.object(sys,'platform','linux'),patch.object(backend,'RESOURCES',resource), \
             patch.object(backend,'bundled_module',return_value=(binary,expected)), \
             patch.object(backend,'check_tools',return_value=[]), \
             patch.object(backend,'run',side_effect=self.fake_run), \
             patch.object(backend,'helper',return_value=types.SimpleNamespace(prepare=self.fake_prepare)), \
             contextlib.redirect_stdout(io.StringIO()):
            backend.install(self.disc,self.support,self.tools,'gcc',{})
            frontend=self.support/'user/config.ini'
            self.assertEqual(frontend.read_text(),preset)
            frontend.write_text('user-customized-settings')
            backend.install(self.disc,self.support,self.tools,'gcc',{})
            self.assertEqual(frontend.read_text(),'user-customized-settings')

if __name__ == '__main__':
    unittest.main()
