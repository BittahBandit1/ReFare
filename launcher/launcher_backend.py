#!/usr/bin/env python3
"""Disc preparation and launch service for the native ReFare desktop GUI.

Standard library only. Each stdout line is one JSON event; child output is
wrapped as log events. Arguments are passed without a shell.
"""
import argparse
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import platform
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

RESOURCES = Path(__file__).resolve().parent
if not (RESOURCES / 'scripts/prepare-game.py').exists():
    RESOURCES = RESOURCES.parent  # running from the source checkout

def emit(event, text='', **values):
    print(json.dumps(dict(event=event, text=text, **values), ensure_ascii=True), flush=True)

def elf_dependencies(path):
    """Inspect ELF64 without loading a user-selected graphics library."""
    with path.open('rb') as source:
        header = source.read(64)
        if len(header) != 64 or header[:6] != b'\x7fELF\x02\x01':
            raise ValueError('Choose a 64-bit Linux Vulkan driver.')
        machine = struct.unpack_from('<H', header, 18)[0]
        expected = 183 if platform.machine().lower() in ('aarch64', 'arm64') else 62
        if machine != expected:
            raise ValueError('This Vulkan driver is built for a different CPU architecture.')
        offset = struct.unpack_from('<Q', header, 32)[0]
        stride, count = struct.unpack_from('<HH', header, 54)
        if stride < 56 or not 0 < count <= 1024:
            raise ValueError('Invalid Vulkan driver library.')
        segments = []
        dynamic = None
        for index in range(count):
            source.seek(offset + index * stride)
            entry = source.read(56)
            if len(entry) != 56:
                raise ValueError('Truncated Vulkan driver library.')
            kind, _, file_offset, address, _, size, _, _ = struct.unpack('<IIQQQQQQ', entry)
            if kind == 1:
                segments.append((address, size, file_offset))
            if kind == 2:
                dynamic = file_offset, size
        if not dynamic or dynamic[1] > 1024 * 1024:
            raise ValueError('The selected file is not a shared Vulkan driver library.')
        source.seek(dynamic[0])
        data = source.read(dynamic[1])
        needed, table = [], None
        for index in range(0, len(data) - 15, 16):
            tag, value = struct.unpack_from('<QQ', data, index)
            if tag == 0: break
            if tag == 1: needed.append(value)
            if tag == 5: table = value
        if table is None:
            raise ValueError('Invalid Vulkan driver string table.')
        base = next((pos + table - start for start, size, pos in segments if start <= table < start + size), None)
        if base is None:
            raise ValueError('Invalid Vulkan driver string table.')
        names = []
        for value in needed:
            source.seek(base + value)
            name = source.read(1024).split(b'\0', 1)[0].decode('utf-8', errors='replace')
            names.append(name)
        return names

def validate_driver(path):
    path = path.resolve()
    if path.suffix.lower() == '.zip':
        raise ValueError('Android driver ZIPs, including the current TurnipZ release, cannot load on native Linux. Choose a Linux Vulkan ICD .json file.')
    if path.suffix.lower() != '.json':
        raise ValueError('Choose the Linux Vulkan driver\'s ICD .json file, rather than its .so library.')
    if path.stat().st_size > 65536:
        raise ValueError('The selected file is not a Vulkan driver manifest.')
    try:
        info = json.loads(path.read_text(encoding='utf-8'))
        library = info['ICD']['library_path']
        version = info['file_format_version']
        if not isinstance(library, str) or not library or not isinstance(version, str):
            raise ValueError()
    except (ValueError, TypeError, KeyError):
        raise ValueError('This is not a Linux Vulkan ICD manifest.') from None
    location = Path(library)
    if not location.is_absolute():
        adjacent = path.parent / location
        if adjacent.is_file():
            location = adjacent.resolve()
        elif '/' in library:
            raise ValueError('The driver library named by this manifest is missing.')
        else:
            # Distribution manifests may use a loader-resolved SONAME.
            result = subprocess.run(['/sbin/ldconfig', '-p'], capture_output=True, text=True, check=False)
            matches = [line.rsplit(' => ', 1)[1].strip() for line in result.stdout.splitlines()
                       if line.strip().startswith(library + ' ') and ' => ' in line]
            if not matches:
                raise ValueError('The driver library named by this manifest could not be located.')
            location = Path(matches[0]).resolve()
    if not location.is_file():
        raise ValueError('The driver library named by this manifest is missing.')
    dependencies = elf_dependencies(location)
    if any(name in dependencies for name in ('libc.so', 'liblog.so', 'libandroid.so', 'libhardware.so', 'libnativewindow.so')):
        raise ValueError('This is an Android/Bionic driver. Native ArmadaOS requires a Linux/glibc Vulkan driver build.')
    info['ICD']['library_path'] = str(location)
    return info

def selected_driver(support):
    settings = support / 'vulkan-driver.json'
    if not settings.exists(): return None
    try:
        value = json.loads(settings.read_text(encoding='utf-8'))['manifest']
        return Path(value) if value else None
    except (OSError, ValueError, TypeError, KeyError):
        raise ValueError('The saved Vulkan driver choice is invalid. Select System in the launcher.') from None

def import_driver(support, path):
    if path.suffix.lower() != '.zip':
        validate_driver(path)
        return path.resolve()
    with zipfile.ZipFile(path) as archive:
        entries = archive.infolist()
        if len(entries) > 512 or sum(entry.file_size for entry in entries) > 256 * 1024 * 1024:
            raise ValueError('The selected driver archive is too large.')
        manifests = []
        for entry in entries:
            name = Path(entry.filename)
            if name.is_absolute() or '..' in name.parts or '\\' in entry.filename or ':' in entry.filename:
                raise ValueError('The driver archive contains an invalid path.')
            if (entry.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError('Extract this driver archive yourself, then select its Linux ICD JSON file.')
            if name.suffix.lower() == '.json' and entry.file_size <= 65536:
                try:
                    info = json.loads(archive.read(entry))
                    if isinstance(info,dict) and isinstance(info.get('ICD'),dict): manifests.append(entry.filename)
                except (ValueError,UnicodeError): pass
        if not manifests:
            raise ValueError('This ZIP has no Linux Vulkan ICD manifest. Android/AdrenoTools packages, including the current TurnipZ release, cannot load on native Fedora.')
        if len(manifests) != 1:
            raise ValueError('This archive contains multiple driver manifests. Extract it and choose the ARM64 ICD JSON file.')
        root = support/'drivers'; root.mkdir(parents=True,exist_ok=True)
        stage = Path(tempfile.mkdtemp(prefix='driver-',dir=root))
        try:
            archive.extractall(stage)
            manifest = stage/manifests[0]
            validate_driver(manifest)
            return manifest.resolve()
        except Exception:
            shutil.rmtree(stage,ignore_errors=True)
            raise

def driver_environment(support, env):
    custom = selected_driver(support)
    result = env.copy()
    if custom:
        info = validate_driver(custom)
        normalized = support / 'selected-vulkan-driver.json'
        temporary = normalized.with_suffix('.json.new')
        temporary.write_text(json.dumps(info), encoding='utf-8')
        os.replace(temporary, normalized)
        result['VK_DRIVER_FILES'] = str(normalized)
        result['VK_ICD_FILENAMES'] = str(normalized)
        emit('log', 'Vulkan driver: ' + str(custom))
    else:
        emit('log', 'Vulkan driver: System')
    return result

def helper(name, filename):
    spec = importlib.util.spec_from_file_location(name, RESOURCES / 'scripts' / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def inspect_disc(path):
    if path.suffix.lower() not in ('.iso', '.ciso'):
        raise ValueError('Choose an .iso or GameCube .ciso disc image.')
    with path.open('rb') as source:
        header = source.read(0x8000)
        compressed = header[:4] == b'CISO'
        if compressed:
            if len(header) != 0x8000 or header[8] != 1:
                raise ValueError('The CISO has no valid disc header.')
            block_size = struct.unpack_from('<I', header, 4)[0]
            if not 0x440 <= block_size <= 32 * 1024 * 1024 or set(header[8:]) - {0, 1}:
                raise ValueError('Invalid GameCube CISO block map.')
            if path.stat().st_size < 0x8000 + sum(header[8:]) * block_size:
                raise ValueError('The CISO image is truncated.')
            header = source.read(0x440)
        if len(header) < 0x440 or header[0x1c:0x20] != bytes.fromhex('c2339f3d'):
            raise ValueError('This is not a GameCube disc image.')
        if header[:6] != b'GCTE51':
            raise ValueError('ReFare needs Crazy Taxi (USA), disc ID GCTE51.')
        if header[6:8] != b'\0\0':
            raise ValueError('ReFare needs disc 1, revision 0 of Crazy Taxi (USA).')
        return compressed

def compiler_environment(compiler):
    env = os.environ.copy()
    if os.name != 'nt' or compiler != 'msvc' or shutil.which('cl', path=env.get('PATH')):
        return env
    # A double-clicked Windows app does not inherit a VS developer terminal.
    # Ask Microsoft's installed locator for the C++ tools, then import only
    # the resulting environment. Disc paths never enter this command string.
    locator = Path(env.get('PROGRAMFILES(X86)', env.get('ProgramFiles(x86)', r'C:\Program Files (x86)'))) / \
              'Microsoft Visual Studio/Installer/vswhere.exe'
    if not locator.is_file():
        return env
    found = subprocess.run([str(locator), '-latest', '-products', '*', '-requires',
                            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
                            '-property', 'installationPath', '-utf8'],
                           capture_output=True, text=True, encoding='utf-8', errors='replace',
                           creationflags=subprocess.CREATE_NO_WINDOW)
    installation = found.stdout.lstrip('\ufeff').strip()
    setup = Path(installation) / 'Common7/Tools/VsDevCmd.bat'
    if not installation or not setup.is_file():
        return env
    env['REFARE_VS_SETUP'] = str(setup)
    configured = subprocess.run(
        ['cmd.exe', '/d', '/q', '/u'],
        input=b'@echo off\r\ncall "%REFARE_VS_SETUP%" -no_logo -arch=x64 -host_arch=x64 >nul\r\nif errorlevel 1 exit 1\r\nset\r\nexit\r\n',
        env=env, capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)
    if configured.returncode != 0:
        return env
    for line in configured.stdout.decode('utf-16-le', errors='replace').splitlines():
        key, separator, value = line.partition('=')
        if separator and key:
            env[key.upper()] = value
    bundled = Path(installation) / 'Common7/IDE/CommonExtensions/Microsoft/CMake'
    extra = [bundled / 'CMake/bin', bundled / 'Ninja']
    env['PATH'] = os.pathsep.join(str(p) for p in extra if p.is_dir()) + os.pathsep + env.get('PATH', '')
    env.pop('REFARE_VS_SETUP', None)
    return env

def bundled_module():
    """Optional personal test module; never generated into public resources."""
    root = RESOURCES / 'native-module'
    try:
        record = json.loads((root / 'manifest.json').read_text(encoding='utf-8'))
        architecture = {'arm64': 'aarch64', 'amd64': 'x86_64'}.get(platform.machine().lower(), platform.machine().lower())
        if record['platform'] != sys.platform or record['architecture'] != architecture:
            return None
        module = root / 'gGCTE51_recomp.so'
        if hashlib.sha256(module.read_bytes()).hexdigest() != record['module_sha256']:
            raise ValueError('The bundled personal game module has changed. Extract the test package again.')
        return module, record['dol_sha256']
    except FileNotFoundError:
        return None

def check_tools(tools, compiler, env, precompiled=False):
    missing = []
    names = ['dolrecomp', 'moderngekko-run']
    if not precompiled: names.append('moderngekko-port')
    for name in names:
        if not executable(tools, name).is_file():
            missing.append('ReFare application files (' + name + ')')
    for name in (() if precompiled else ('cmake', 'ninja', {'msvc': 'cl', 'gcc': 'gcc', 'clang': 'clang'}[compiler])):
        if not shutil.which(name, path=env.get('PATH')):
            missing.append(name)
    if not precompiled and platform.system() == 'Linux' and compiler == 'clang' and not shutil.which('ld.lld', path=env.get('PATH')):
        missing.append('lld')
    return missing

def executable(directory, name):
    return directory / (name + ('.exe' if os.name == 'nt' else ''))

def run(command, support, env):
    flags = {'creationflags': subprocess.CREATE_NO_WINDOW} if os.name == 'nt' else {}
    with subprocess.Popen([str(arg) for arg in command], cwd=support, env=env,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, encoding='utf-8', errors='replace', bufsize=1, **flags) as child:
        for line in child.stdout:
            emit('log', line.rstrip())
        code = child.wait()
    if code != 0:
        raise RuntimeError('The operation failed (exit code ' + str(code) + '). See the log for details.')

def load_installation(support, suffix=None):
    try:
        record = json.loads((support / 'installation.json').read_text(encoding='utf-8'))
        if not isinstance(record, dict) or record.get('version') != 1:
            return None
        if record.get('platform') != sys.platform or record.get('architecture') != platform.machine().lower():
            return None
        game = support / record['game']
        module = support / record['module']
        # Saved paths must stay in our library; reject malformed/foreign markers.
        root = support.resolve()
        for path in (game, module):
            if root not in path.resolve().parents:
                return None
        suffix = suffix or ('.dll' if os.name == 'nt' else '.dylib' if sys.platform == 'darwin' else '.so')
        if not module.is_file() or module.suffix != suffix or not (game / 'files/ct').exists():
            return None
        data = (game / 'sys/main.dol').read_bytes()
        if hashlib.sha256(data).hexdigest() != record['dol_sha256']:
            return None
        return game, module
    except (OSError, ValueError, KeyError, TypeError):
        return None

def install(disc, support, tools, compiler, env):
    disc, support, tools = disc.resolve(), support.resolve(), tools.resolve()
    compressed = inspect_disc(disc)
    precompiled = bundled_module()
    missing = check_tools(tools, compiler, env, precompiled=bool(precompiled))
    if missing:
        emit('missing_tools', 'A few free developer tools are needed to prepare the game.', missing=missing)
        return
    installs = support / 'installs'
    installs.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix='game-', dir=installs))
    committed = False
    try:
        image = disc
        if compressed:
            emit('status', 'Converting the CISO disc image…')
            image = stage / 'converted.iso'
            with contextlib.redirect_stdout(io.StringIO()):
                helper('ciso', 'ciso-to-iso.py').convert(disc, image)
        emit('status', 'Extracting the disc…')
        extracted, game = stage / 'extracted', stage / 'prepared'
        run([executable(tools, 'dolrecomp'), 'extract', '--native-only', image, extracted], support, env)
        emit('status', 'Preparing the game files…')
        with contextlib.redirect_stdout(io.StringIO()):
            helper('prepare', 'prepare-game.py').prepare(extracted, game, True)
        assets = game / 'files'
        if assets.is_symlink():
            assets.unlink()
            shutil.move(str(extracted / 'files'), assets)
        shutil.rmtree(extracted)
        if compressed:
            image.unlink()
        modules = stage / 'modules'
        suffix = '.dll' if os.name == 'nt' else '.dylib' if sys.platform == 'darwin' else '.so'
        if precompiled:
            emit('status', 'Preparing the native game module…')
            binary, expected = precompiled
            if hashlib.sha256((game / 'sys/main.dol').read_bytes()).hexdigest() != expected:
                raise ValueError('This disc does not match the personal test build\'s native module.')
            module = modules / 'GCTE51/precompiled/gGCTE51_recomp.so'
            module.parent.mkdir(parents=True)
            shutil.copy2(binary, module)
            active = modules / 'GCTE51/active-module.txt'
            active.write_text(str(module), encoding='utf-8')
        else:
            emit('status', 'Recompiling Crazy Taxi. This takes a few minutes, once.')
            run([executable(tools, 'moderngekko-port'), 'build', game, '--backend', 'c',
                 '--toolchain', compiler, '--output', modules], support, env)
        # The compiler leaves a second copy in module-build/. Its published
        # active-module pointer identifies the one the runner should load.
        active = modules / 'GCTE51/active-module.txt'
        module = Path(active.read_text(encoding='utf-8').strip())
        if not module.is_absolute():
            module = support / module
        module = module.resolve()
        if (modules.resolve() not in module.parents or not module.is_file() or
                module.name != 'gGCTE51_recomp' + suffix or not (game / 'files/ct').exists()):
            raise RuntimeError('Preparation did not produce a complete game and native module.')
        gfx = support / 'user/Config/GFX.ini'
        if not gfx.exists():
            gfx.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(RESOURCES / 'config/GFX.ini', gfx)
        frontend = support / 'user/config.ini'
        preset = RESOURCES / 'config/frontend.ini'
        if preset.is_file() and not frontend.exists():
            shutil.copy2(preset,frontend)
        record = {'version': 1, 'platform': sys.platform, 'architecture': platform.machine().lower(),
                  'game': str(game.relative_to(support)),
                  'module': str(module.relative_to(support)),
                  'dol_sha256': hashlib.sha256((game / 'sys/main.dol').read_bytes()).hexdigest()}
        pending = support / 'installation.json.new'
        pending.write_text(json.dumps(record), encoding='utf-8')
        os.replace(pending, support / 'installation.json')
        committed = True
        emit('ready', 'Crazy Taxi is ready to play.')
    finally:
        if not committed:
            shutil.rmtree(stage, ignore_errors=True)

@contextlib.contextmanager
def operation_lock(support):
    # OS locks disappear even after a crash, unlike an exclusive lock-file
    # marker. Multiple launcher windows cannot modify or run one library at once.
    with (support / 'operation.lock').open('a+b') as lock:
        try:
            if os.name == 'nt':
                import msvcrt
                if lock.tell() == 0:
                    lock.write(b'\0'); lock.flush()
                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            raise RuntimeError('Another ReFare window is preparing or running this game.') from error
        try:
            yield
        finally:
            if os.name == 'nt':
                lock.seek(0); msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(lock, fcntl.LOCK_UN)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('status', 'check', 'install', 'play', 'driver'))
    parser.add_argument('--support-dir', type=Path, required=True)
    parser.add_argument('--tools-dir', type=Path, required=True)
    parser.add_argument('--source-dir', type=Path, required=True)
    parser.add_argument('--compiler', choices=('msvc', 'gcc', 'clang'), required=True)
    parser.add_argument('--disc', type=Path)
    parser.add_argument('--graphics', default='Metal' if sys.platform == 'darwin' else 'Vulkan')
    choice = parser.add_mutually_exclusive_group()
    choice.add_argument('--driver-json', type=Path)
    choice.add_argument('--system-driver', action='store_true')
    args = parser.parse_args()
    support, tools = args.support_dir.resolve(), args.tools_dir.resolve()
    support.mkdir(parents=True, exist_ok=True)
    if sys.platform.startswith('linux') and args.action in ('status', 'driver'):
        if args.action == 'driver':
            value = None
            if args.driver_json:
                value = str(import_driver(support,args.driver_json))
            temporary = support / 'vulkan-driver.json.new'
            temporary.write_text(json.dumps({'manifest': value}), encoding='utf-8')
            os.replace(temporary, support / 'vulkan-driver.json')
        selected = selected_driver(support)
        emit('driver', selected.name if selected else 'System')
    elif args.action == 'driver':
        raise ValueError('Custom Linux Vulkan drivers are only supported on Linux.')
    if args.action in ('status', 'driver'):
        emit('ready' if load_installation(support) else 'needs_disc')
        return
    if args.action == 'play':
        installed = load_installation(support)
        if not installed:
            raise ValueError('The prepared game is missing or has changed. Please import your disc again.')
        env = os.environ.copy()
        env['PYTHONUNBUFFERED'] = '1'
        if sys.platform.startswith('linux') and args.graphics == 'Vulkan':
            env = driver_environment(support, env)
        game, module = installed
        command = [executable(tools, 'moderngekko-run'), '--game', game, '--module', module,
                   '--user-dir', support / 'user', '--graphics', args.graphics]
        if sys.platform.startswith('linux') and env.get('WAYLAND_DISPLAY') and not env.get('DISPLAY'):
            command.append('--wayland')
        emit('status', 'Crazy Taxi is running. Quit the game to come back here.')
        with operation_lock(support):
            run(command, support, env)
        emit('ready', 'Crazy Taxi is ready to play.')
        return
    env = compiler_environment(args.compiler)
    env['MODERNGEKKO_SOURCE_DIR'] = str(args.source_dir.resolve())
    env['PYTHONUNBUFFERED'] = '1'
    if args.action == 'check':
        missing = check_tools(tools, args.compiler, env, precompiled=bool(bundled_module()))
        emit('missing_tools' if missing else 'needs_disc', missing=missing)
    else:
        if not args.disc:
            parser.error('install requires --disc')
        with operation_lock(support):
            install(args.disc.resolve(), support, tools, args.compiler, env)

if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        emit('error', str(error))
        sys.exit(1)
