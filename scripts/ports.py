#!/usr/bin/env python3
"""Native desktop builds, tests and disc preparation without a Unix shell.

Run from a VS 2022 x64 developer terminal on Windows. Cross compilation is
intentionally separate: the host recompiler must remain executable on the host.
"""
import argparse
import importlib.util
import os
from pathlib import Path
import platform
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
TARGETS = {'macos-arm64': ('Darwin', 'arm64', 'Metal'),
           'windows-x86_64': ('Windows', 'x86_64', 'Vulkan'),
           'linux-x86_64': ('Linux', 'x86_64', 'Vulkan'),
           'linux-arm64': ('Linux', 'arm64', 'Vulkan')}

def host_target():
    machine = platform.machine().lower()
    arch = {'amd64': 'x86_64', 'aarch64': 'arm64'}.get(machine, machine)
    for name, (system, architecture, _) in TARGETS.items():
        if (platform.system(), arch) == (system, architecture):
            return name
    raise ValueError(f'Unsupported build host: {platform.system()} {machine}')

def run(*args, env=None):
    command = [str(arg) for arg in args]
    print('> ' + subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, cwd=ROOT, env=env, check=True)

def apply_patches():
    run(sys.executable, ROOT / 'scripts/apply-patches.py')

def configure(target, testing):
    if target != host_target():
        raise ValueError(f'{target} requires a native host; detected {host_target()}. '
                         'Use the matching CI runner or machine.')
    apply_patches()
    build = ROOT / 'build/ports' / target
    args = ['-DCMAKE_BUILD_TYPE=Release', f'-DBUILD_TESTING={"ON" if testing else "OFF"}',
            '-DMODERNGEKKO_FRONTEND_NAME=ReFare',
            '-DMODERNGEKKO_DEFAULT_WINDOW_TITLE=ReFare',
            '-DMODERNGEKKO_USER_DIRECTORY_NAME=ReFare',
            '-DMODERNGEKKO_GAMECUBE_CONTROLLERS=ON',
            '-DMODERNGEKKO_REQUIRED_DISC_ID=GCTE51',
            f'-DREFARE_OPTIONS_SOURCE_DIR={ROOT / "src"}',
            '-DBUNDLE_SPEEX=ON',
            '-DDOLRECOMP_ENABLE_LLVM=OFF']
    if target == 'macos-arm64':
        args += ['-DCMAKE_OSX_ARCHITECTURES=arm64']
    if target.startswith('linux-'):
        args += ['-DENABLE_X11=ON', '-DENABLE_WAYLAND=ON']
    run('cmake', '-S', ROOT / 'lib/ModernGekko', '-B', build, '-G', 'Ninja', *args)
    return build

def prepare(build, target, disc, extracted, skip):
    # Imported helpers avoid spawning a shell and retain the revision guard.
    sys.path.insert(0, str(ROOT / 'scripts'))
    spec = importlib.util.spec_from_file_location('prepare_game', ROOT / 'scripts/prepare-game.py')
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    suffix = '.exe' if target.startswith('windows-') else ''
    original = ROOT / 'extracted/Crazy-Taxi-USA'
    if extracted:
        original = extracted.resolve()
    elif disc:
        original = ROOT / 'build/ports' / target / 'imported'
        if original.exists():
            raise ValueError(f'Import destination already exists: {original}. '
                             'Use --extracted to reuse it or choose a fresh directory.')
        disc = disc.resolve()
        if disc.suffix.lower() == '.ciso':
            ciso_spec = importlib.util.spec_from_file_location('ciso', ROOT / 'scripts/ciso-to-iso.py')
            ciso = importlib.util.module_from_spec(ciso_spec)
            ciso_spec.loader.exec_module(ciso)
            converted = build / 'personal-disc.iso'
            ciso.convert(disc, converted)
            disc = converted
        run(build / ('dolrecomp' + suffix), 'extract', '--native-only', disc, original)
    staged = build / 'game'
    helper.prepare(original, staged, skip)
    return staged

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['configure', 'build', 'test', 'run', 'launcher'])
    parser.add_argument('--target', choices=list(TARGETS))
    parser.add_argument('--jobs', type=int, default=min(os.cpu_count() or 4, 8))
    source = parser.add_mutually_exclusive_group()
    source.add_argument('--disc', type=Path)
    source.add_argument('--extracted', type=Path)
    parser.add_argument('--original-startup', action='store_true')
    parser.add_argument('--graphics', choices=['Metal', 'Vulkan', 'D3D', 'OGL'])
    parser.add_argument('--window-system', choices=['x11', 'wayland'],
                        help='Choose the Linux window backend explicitly')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    target = args.target or host_target()
    if args.window_system and not target.startswith('linux-'):
        parser.error('--window-system applies only to Linux')
    build = configure(target, args.action == 'test')
    if args.action == 'configure':
        return
    targets = ['moderngekko-port', 'refare-launcher']
    if args.action == 'test':
        targets += ['refare-options-test']
    run('cmake', '--build', build, '--parallel', args.jobs, '--target', *targets)
    if args.action == 'test':
        # Root tests cover the actual ReFare menu and runtime service behavior.
        suffix = '.exe' if target.startswith('windows-') else ''
        run(build / ('refare-options-test' + suffix), build / 'test-user')
        smoke_env = os.environ.copy()
        smoke_env.update(SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software')
        run(build / ('ReFare' + suffix), '--smoke-test',
            '--support-dir', build / 'launcher-test-user', env=smoke_env)
        run(build / ('ReFare' + suffix), '--probe-service',
            '--support-dir', build / 'launcher-test-user', env=smoke_env)
        run(sys.executable, '-m', 'unittest', 'discover', '-s', str(ROOT / 'tests'),
            '-p', 'portability_test.py')
        run(sys.executable, '-m', 'unittest', 'discover', '-s', str(ROOT / 'tests'),
            '-p', 'launcher_test.py')
        if (ROOT / 'extracted/Crazy-Taxi-USA/sys/main.dol').exists():
            run(sys.executable, ROOT / 'tests/startup_patch_test.py')
        return
    if args.action == 'launcher':
        run(build / ('ReFare.exe' if target.startswith('windows-') else 'ReFare'))
        return
    if args.action == 'run':
        staged = prepare(build, target, args.disc, args.extracted, not args.original_startup)
        suffix = '.exe' if target.startswith('windows-') else ''
        profile = build / 'user'
        if not (profile / 'Config/GFX.ini').exists():
            run(sys.executable, ROOT / 'scripts/configure-video.py', '--user-dir', profile)
        window_system = args.window_system
        if target.startswith('linux-') and not window_system:
            if os.environ.get('WAYLAND_DISPLAY') and not os.environ.get('DISPLAY'):
                window_system = 'wayland'
        run(build / ('moderngekko-port' + suffix), 'run', staged, '--backend', 'c',
            '--output', build / 'modules', '--', '--graphics', args.graphics or TARGETS[target][2],
            '--user-dir', profile, *(['--' + window_system] if window_system else []))

if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f'error: {error}', file=sys.stderr)
        sys.exit(1)
