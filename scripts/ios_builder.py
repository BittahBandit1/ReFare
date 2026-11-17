#!/usr/bin/env python3
"""Build the iOS 15 on-device AOT builder. No personal game input is needed.

Requires Xcode, CMake/Ninja and host LLVM 20 (llvm-tblgen/clang-tblgen).
The resulting builder exports unsigned personal IPAs, not installed apps.
"""
import argparse
import hashlib
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import struct
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
VERSION = '20.1.8'
SOURCE_SHA = '6898f963c8e938981e6c4a302e83ec5beb4630147c7311183cf61069af16333d'

def run(*args, **kwargs):
    environment = os.environ.copy()
    environment.setdefault('CLANG_MODULE_CACHE_PATH', str(ROOT/'build/ios-toolchain/module-cache'))
    environment.setdefault('SWIFT_MODULECACHE_PATH', str(ROOT/'build/ios-toolchain/module-cache'))
    environment.setdefault('CCACHE_DIR', '/private/tmp/refare-ccache')
    subprocess.run(list(map(str, args)), cwd=ROOT, check=True, env=environment, **kwargs)

def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()

def sources(base):
    source = base / f'llvm-project-{VERSION}.src'
    if (source / 'lld/include/lld/Common/Driver.h').is_file():
        return source
    archive = ROOT / f'build/ios-llvm-{VERSION}.tar.xz'
    if not archive.is_file():
        urllib.request.urlretrieve(f'https://github.com/llvm/llvm-project/releases/download/llvmorg-{VERSION}/llvm-project-{VERSION}.src.tar.xz', archive)
    if digest(archive) != SOURCE_SHA:
        raise ValueError('LLVM source checksum mismatch')
    base.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as bundle:
        prefix = source.name + '/'
        selected = [m for m in bundle.getmembers() if m.name.startswith(prefix) and
                    m.name[len(prefix):].split('/')[0] in ('llvm', 'clang', 'lld', 'cmake', 'third-party', 'LICENSE.TXT')]
        bundle.extractall(base, members=selected, filter='data')
    return source

def build_compiler(source, build, host, jobs):
    run('cmake', '-S', source/'llvm', '-B', build, '-G', 'Ninja',
        '-DCMAKE_SYSTEM_NAME=iOS', '-DCMAKE_OSX_SYSROOT=iphoneos', '-DCMAKE_OSX_ARCHITECTURES=arm64',
        '-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0', '-DCMAKE_BUILD_TYPE=MinSizeRel', '-DCMAKE_MACOSX_BUNDLE=OFF',
        '-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY', '-DLLVM_ENABLE_PROJECTS=clang;lld',
        '-DLLVM_TARGETS_TO_BUILD=AArch64', '-DLLVM_HOST_TRIPLE=arm64-apple-ios15.0',
        '-DLLVM_DEFAULT_TARGET_TRIPLE=arm64-apple-ios15.0',
        f'-DLLVM_TABLEGEN={host}/bin/llvm-tblgen', f'-DCLANG_TABLEGEN={host}/bin/clang-tblgen',
        *[f'-D{option}=OFF' for option in ('LLVM_INCLUDE_TESTS', 'LLVM_INCLUDE_EXAMPLES', 'LLVM_INCLUDE_BENCHMARKS',
          'LLVM_INCLUDE_DOCS', 'LLVM_ENABLE_ZLIB', 'LLVM_ENABLE_ZSTD', 'LLVM_ENABLE_LIBXML2',
          'LLVM_ENABLE_LIBEDIT', 'LLVM_ENABLE_BACKTRACES', 'LLVM_ENABLE_CRASH_OVERRIDES',
          'LLVM_ENABLE_ASSERTIONS', 'LLVM_ENABLE_BINDINGS', 'LLVM_BUILD_TOOLS', 'CLANG_BUILD_TOOLS',
          'LLD_BUILD_TOOLS', 'CLANG_ENABLE_ARCMT', 'CLANG_ENABLE_STATIC_ANALYZER')],
        '-DLLVM_ENABLE_LIBCXX=ON', '-DLLVM_ENABLE_PIC=ON', '-DHAVE_LIBRT=OFF')
    run('cmake', '--build', build, '--target', 'clangCodeGen', 'clangFrontend', 'clangDriver',
        'LLVMAArch64CodeGen', 'LLVMAArch64AsmParser', 'lldMachO', '--parallel', jobs)

def gameplay_template(jobs):
    build = ROOT/'build/ports/ios-export-template'
    run(sys.executable, ROOT/'scripts/apply-patches.py')
    run('cmake', '-S', ROOT/'lib/ModernGekko', '-B', build, '-G', 'Ninja',
        '-DCMAKE_SYSTEM_NAME=iOS', '-DCMAKE_OSX_SYSROOT=iphoneos', '-DCMAKE_OSX_ARCHITECTURES=arm64',
        '-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0', '-DCMAKE_BUILD_TYPE=Release',
        f'-DCMAKE_Swift_FLAGS=-target arm64-apple-ios15.0 -module-cache-path "{ROOT}/build/ios-toolchain/module-cache"', '-DBUILD_TESTING=OFF',
        '-DMODERNGEKKO_ENABLE_DYNAMIC_MODULES=OFF', '-DENABLE_VULKAN=OFF', '-DUSE_BUNDLED_MOLTENVK=OFF',
        f'-DREFARE_IOS_APP_SOURCE_DIR={ROOT / "ports/ios"}', '-DREFARE_IOS_EXPORT_TEMPLATE=ON')
    run('cmake', '--build', build, '--target', 'ReFareImport', '--parallel', jobs)
    return build/'refare-ios/ReFareImport.app'

def prepare_assets(base, source, template):
    import re
    info = plistlib.loads((template/'Info.plist').read_bytes())
    executable = template/info['CFBundleExecutable']
    symbols = subprocess.check_output(['xcrun', 'nm', '--defined-only', str(executable)], text=True)
    if re.search(r'\b_func_[0-9A-Fa-f]{8}\b', symbols):
        raise ValueError('Refusing to bundle a template containing precompiled game chunks')
    if (template/'PersonalGame').exists() or any(p.suffix.lower() in ('.dol', '.iso', '.ciso', '.gcm') for p in template.rglob('*')):
        raise ValueError('Refusing to bundle personal disc data in the builder')
    assets = base/'assets'
    if assets.exists(): shutil.rmtree(assets)
    tools = assets/'Toolchain'
    tools.mkdir(parents=True)
    dolphin = ROOT/'lib/ModernGekko/vendor/dolphin'
    app = base/'template-staging.app'
    if app.exists(): shutil.rmtree(app)
    shutil.copytree(template, app)
    # No signature/profile is valid after the personal framework/assets are added.
    shutil.rmtree(app/'_CodeSignature', ignore_errors=True)
    (app/'embedded.mobileprovision').unlink(missing_ok=True)
    info = plistlib.loads((app/'Info.plist').read_bytes())
    info.update(CFBundleIdentifier='io.github.bittahbandit1.refare.play', CFBundleDisplayName='ReFare', CFBundleName='ReFare',
                CFBundleShortVersionString='0.2.5', CFBundleVersion='15')
    (app/'Info.plist').write_bytes(plistlib.dumps(info))
    shutil.copytree(ROOT/'ports/ios/compiler-include', tools/'cinclude')
    (tools/'include/core').mkdir(parents=True)
    for name in ('cpu.h', 'types.h'):
        shutil.copy2(dolphin/'GXRuntime/include/core'/name, tools/'include/core'/name)
    (tools/'abi').mkdir()
    shutil.copy2(dolphin/'Source/Core/Core/PowerPC/StaticRecomp/StaticRecompABI.h', tools/'abi')
    shutil.copy2(dolphin/'module-template/module_export.c', tools)
    helpers = base/'helpers'
    helpers.mkdir(exist_ok=True)
    objects = []
    for name in ('cpu', 'cpu_exception', 'cpu_interpreter', 'cpu_interpreter_table', 'cpu_interpreter_float', 'cpu_interpreter_integer'):
        output = helpers/(name + '.o')
        run('xcrun', '--sdk', 'iphoneos', 'clang', '-target', 'arm64-apple-ios15.0', '-O2', '-fPIC', '-fvisibility=hidden',
            '-I', dolphin/'GXRuntime/include', '-c', dolphin/f'GXRuntime/src/core/{name}.c', '-o', output)
        objects.append(output)
    archive = tools/'runtime-helpers.a'
    run('xcrun', 'libtool', '-static', '-o', archive, *objects)
    # Only ABI symbol names are needed to link against iOS's libSystem. Do not
    # redistribute an Apple SDK. CPU helpers are ordinary redistributable code.
    nm = subprocess.check_output(['xcrun', 'nm', '-u', *map(str, objects)], text=True)
    defined = subprocess.check_output(['xcrun', 'nm', '-gU', *map(str, objects)], text=True)
    # Apple's nm -u prints bare names; llvm-nm may retain the U column.
    symbols = set(re.findall(r'^\s*(?:U\s+)?(_[A-Za-z0-9_.$]+)\s*$', nm, re.M))
    symbols -= set(re.findall(r'\b[TDBS] (\S+)$', defined, re.M))
    symbols.update(('dyld_stub_binder', '_memcpy', '_memmove', '_memset', '_memcmp', '_fabs', '_fabsf', '_sqrt', '_sqrtf',
                    '_fma', '_fmaf', '_copysign', '_copysignf', '_floor', '_ceil', '_trunc', '_round', '_nearbyint', '_rint', '_fmod'))
    (tools/'libSystem.tbd').write_text('--- !tapi-tbd-v3\narchs: [ arm64 ]\nplatform: ios\ninstall-name: /usr/lib/libSystem.B.dylib\n'
        'exports:\n  - archs: [ arm64 ]\n    symbols: [ ' + ', '.join(sorted(symbols)) + ' ]\n...\n')
    run('cmake', f'-DREFARE_STARTUP_JSON={ROOT}/config/startup-patches.json',
        f'-DREFARE_STARTUP_CSV={assets}/startup-patches.csv', '-P', ROOT/'ports/ios/StartupPatches.cmake')
    licenses = assets/'Licenses'
    licenses.mkdir()
    for origin, name in ((ROOT/'LICENSE', 'ReFare-GPL-3.0.txt'), (source/'LICENSE.TXT', 'LLVM-Apache-2.0-with-exceptions.txt'),
                         (dolphin/'DolRecomp/LICENSE', 'DolRecomp.txt'), (dolphin/'COPYING', 'Dolphin.txt'),
                         (dolphin/'GXRuntime/LICENSE', 'GXRuntime.txt'), (dolphin/'GXRuntime/THIRD_PARTY.md', 'GXRuntime-third-party.md')):
        if origin.is_file(): shutil.copy2(origin, licenses/name)
    shutil.copytree(licenses, app/'Licenses', dirs_exist_ok=True)
    write_template(app, assets/'GameplayTemplate.rfarchive')
    shutil.rmtree(app)
    # Bind caches to the complete compiler, ABI, template and builder implementation.
    identity = hashlib.sha256(b'refare-ios-aot-v1')
    inputs = list(assets.rglob('*')) + list((base/'iphoneos/lib').glob('*.a')) + list((ROOT/'ports/ios').glob('*.*'))
    for path in sorted(p for p in inputs if p.is_file()):
        identity.update(path.name.encode()); identity.update(bytes.fromhex(digest(path)))
    (assets/'compiler-id.txt').write_text(identity.hexdigest() + '\n')
    return assets

def write_template(app, output):
    files = sorted(p for p in app.rglob('*') if p.is_file())
    if any(p.is_symlink() for p in app.rglob('*')): raise ValueError('Unexpected template symlink')
    with output.open('wb') as archive:
        archive.write(b'RFTPL001' + struct.pack('<I', len(files)))
        for file in files:
            name = file.relative_to(app).as_posix().encode()
            mode = 0o755 if file.stat().st_mode & 0o111 else 0o644
            archive.write(struct.pack('<HIQ', len(name), mode, file.stat().st_size))
            archive.write(bytes.fromhex(digest(file)))
            archive.write(name)
            with file.open('rb') as source: shutil.copyfileobj(source, archive)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host-llvm', type=Path, default=Path('/opt/homebrew/opt/llvm@20'))
    parser.add_argument('--jobs', type=int, default=6)
    parser.add_argument('--skip-compiler-build', action='store_true', help='Reuse a completed embedded LLVM build')
    args = parser.parse_args()
    if args.jobs < 1: parser.error('--jobs must be positive')
    if sys.platform != 'darwin': parser.error('Xcode on macOS is required to build the builder itself')
    base = ROOT/'build/ios-toolchain'
    source = sources(base)
    # LLVM TableGen's CMake rules do not quote parentheses in source paths.
    alias = Path('/private/tmp/refare-ios-toolchain')
    if alias.is_symlink():
        if alias.resolve() != base.resolve(): raise ValueError('Temporary compiler path belongs to a different checkout')
    elif alias.exists(): raise ValueError('Temporary compiler path already exists')
    else: alias.symlink_to(base, target_is_directory=True)
    source_alias = alias/source.name
    if not args.skip_compiler_build: build_compiler(source_alias, alias/'iphoneos', args.host_llvm, args.jobs)
    template = gameplay_template(args.jobs)
    assets = prepare_assets(base, source, template)
    build = ROOT/'build/ports/ios-builder'
    run('cmake', '-S', ROOT/'ports/ios', '-B', build, '-G', 'Xcode',
        '-DCMAKE_SYSTEM_NAME=iOS', '-DCMAKE_OSX_SYSROOT=iphoneos', '-DCMAKE_OSX_ARCHITECTURES=arm64',
        '-DCMAKE_C_COMPILER=' + subprocess.check_output(['xcrun', '--find', 'clang'], text=True).strip(),
        '-DCMAKE_CXX_COMPILER=' + subprocess.check_output(['xcrun', '--find', 'clang++'], text=True).strip(),
        '-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0', '-DCMAKE_SUPPRESS_REGENERATION=ON', '-DREFARE_IOS_BUILDER=ON',
        f'-DREFARE_IOS_LLVM_SOURCE={source_alias}', f'-DREFARE_IOS_LLVM_BUILD={alias}/iphoneos',
        f'-DREFARE_IOS_BUILDER_ASSETS={assets}')
    run('cmake', '--build', build, '--config', 'Release', '--target', 'ReFareImport', '--parallel', args.jobs,
        '--', 'CODE_SIGNING_ALLOWED=NO')
    app = build/'Release-iphoneos/ReFareImport.app'
    if not (app/'BuilderAssets/GameplayTemplate.rfarchive').is_file() or not (app/'BuilderAssets/Toolchain/runtime-helpers.a').is_file():
        raise ValueError('Built app is missing its template/compiler resources')
    info = plistlib.loads((app/'Info.plist').read_bytes())
    info.update(CFBundleName='ReFare Builder', CFBundleDisplayName='ReFare Builder', CFBundleShortVersionString='0.2.5', CFBundleVersion='15')
    (app/'Info.plist').write_bytes(plistlib.dumps(info))
    sys.path.insert(0, str(ROOT/'scripts'))
    from ios import package_ipa
    output = package_ipa(build, label='Builder')
    print(f'Builder IPA: {output}\nExperimental: sign the builder, then sign each personal gameplay IPA it exports.')

if __name__ == '__main__':
    try: main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f'error: {error}', file=sys.stderr); sys.exit(1)
