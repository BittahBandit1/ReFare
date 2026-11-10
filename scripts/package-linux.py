#!/usr/bin/env python3
"""Create a relocatable Linux test folder, retaining the host graphics stack."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parent.parent

# Keep glibc, compiler runtimes, and the graphics/DRM stack supplied by the
# target Fedora installation. Bundling older Mesa dependencies can break a
# newer device driver. Application libraries use RUNPATH, never LD_LIBRARY_PATH.
HOST_LIBRARIES = re.compile(r'^(ld-linux|lib(c|m|dl|rt|pthread|resolv|util|anl)\.so|'
                            r'libstdc\+\+|libgcc_s|libGL|libEGL|libOpenGL|libvulkan|'
                            r'libdrm|libgbm|libLLVM|libgallium|libnvidia)')

def dependencies(binary):
    output = subprocess.check_output(['ldd', str(binary)], text=True)
    if 'not found' in output:
        raise ValueError('Unresolved build dependency:\n' + output)
    return [Path(match) for match in re.findall(r'=>\s+(/\S+)', output)]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, default=ROOT/'build/release')
    parser.add_argument('--personal-module', type=Path)
    parser.add_argument('--prepared-dol', type=Path)
    args = parser.parse_args()
    if platform.system() != 'Linux' or platform.machine() != 'aarch64':
        parser.error('Run this packaging step inside the ARM64 Linux builder')
    if bool(args.personal_module) != bool(args.prepared_dol):
        parser.error('--personal-module and --prepared-dol must be supplied together')
    build = args.build_dir.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    folder = output/'ReFare-Linux-arm64-SD865-test'
    if folder.exists(): shutil.rmtree(folder)
    folder.mkdir()
    libraries = folder/'lib'; libraries.mkdir()
    binaries = []
    for name in ('ReFare','moderngekko-run','moderngekko-port','dolrecomp'):
        source = build/name
        if not source.is_file(): raise ValueError('Missing build output: '+str(source))
        shutil.copy2(source,folder/name); binaries.append(folder/name)
    shutil.copytree(build/'Sys',folder/'Sys')
    shutil.copytree(build/'refare-data',folder/'refare-data',
                    ignore=shutil.ignore_patterns('__pycache__','*.pyc','native-module'))
    (folder/'refare-data/config/frontend.ini').write_text(
        '[Video]\nresolution=640x528\nbackend=Vulkan\nfullscreen=true\n'
        'metal_hud=false\nshow_fps_in_title=true\n[Input]\n',encoding='utf-8')
    if args.personal_module:
        native = folder/'refare-data/native-module'; native.mkdir()
        module = native/'gGCTE51_recomp.so'
        shutil.copy2(args.personal_module,module)
        info = {'platform':'linux','architecture':'aarch64',
                'module_sha256':hashlib.sha256(module.read_bytes()).hexdigest(),
                'dol_sha256':hashlib.sha256(args.prepared_dol.read_bytes()).hexdigest()}
        (native/'manifest.json').write_text(json.dumps(info,indent=2))
        binaries.append(module)
    pending = list(binaries)
    copied = set()
    while pending:
        binary = pending.pop()
        for source in dependencies(binary):
            if HOST_LIBRARIES.match(source.name) or source.name in copied: continue
            destination = libraries/source.name
            shutil.copy2(source.resolve(),destination)
            copied.add(source.name); pending.append(destination)
    for binary in binaries:
        if binary.parent == folder:
            rpath = '$ORIGIN/lib'
            subprocess.run(['patchelf','--set-rpath',rpath,str(binary)],check=True)
    for library in libraries.iterdir():
        subprocess.run(['patchelf','--set-rpath','$ORIGIN',str(library)],check=True)
    if args.personal_module:
        # RUNPATH editing changes the ELF bytes; record the delivered hash.
        manifest = folder/'refare-data/native-module/manifest.json'
        info = json.loads(manifest.read_text())
        info['module_sha256'] = hashlib.sha256((manifest.parent/'gGCTE51_recomp.so').read_bytes()).hexdigest()
        manifest.write_text(json.dumps(info,indent=2))
    (folder/'README.txt').write_text('''ReFare — Linux ARM64 / Snapdragon 865 personal test build

Extract this entire folder, open a terminal here, and run:
  ./ReFare

Choose your Crazy Taxi USA (GCTE51 revision 0) ISO or CISO. This personal
package includes a module compiled from your disc, so importing does not
require installing GCC, CMake, or Ninja on ArmadaOS. Python 3 is required.
Your saves and imported files are stored in ~/.local/share/ReFare.

Start with native resolution (1x), VSync on, interpolation off. Settings can
be changed with the left shoulder/L1 menu after launching the game.

Vulkan driver: use the launcher’s “Vulkan driver…” button. “System driver”
uses ArmadaOS’s configured driver. A custom driver must be a native Linux
ARM64 Vulkan ICD JSON with its matching Linux shared library. Selection
applies only to ReFare’s next game process. It does not replace OS drivers.
The current TurnipZ 0.3.0 Android/AdrenoTools ZIP cannot load on native Fedora.

This archive does not contain game assets. Its precompiled game module is
personal game-derived code: keep this test bundle private, not in GitHub releases.
The open-source runtime is GPL-3.0; see refare-data/LICENSE and SOURCE.txt.

The build is tested in an ARM64 Linux VM, not on an Adreno 650. Please keep
launcher.log and user/Logs from the ReFare data folder if a test fails.
''',encoding='utf-8')
    (folder/'SOURCE.txt').write_text('''ReFare: https://github.com/BittahBandit1/ReFare
Base source commit: a5b2ddced7bd9a4779201608cecb19caa43b84be
This testing build also includes local port/launcher changes. The exact source
snapshot is provided beside the test archive as ReFare-Linux-arm64-test-source.tar.gz.
The personal game module is built separately from the player’s own disc and
is not part of the open-source runtime snapshot.
''')
    archive = output/(folder.name+'.tar.gz')
    with tarfile.open(archive,'w:gz',compresslevel=6) as tar:
        tar.add(folder,arcname=folder.name)
    print(archive)
    print('Application libraries bundled:',len(copied))
    print('SHA256:',hashlib.sha256(archive.read_bytes()).hexdigest())

if __name__ == '__main__':
    main()
