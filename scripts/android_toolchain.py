"""Prepare checksum-pinned Android-native Clang/LLD and generic module sources.

Compiler executables live in the APK native-library directory; generated code
and non-executable headers/sources live only in app-private storage.
"""
from pathlib import Path
import concurrent.futures
import hashlib
import io
import json
import re
import shutil
import struct
import subprocess
import tarfile
import zipfile

ROOT=Path(__file__).resolve().parents[1]
SYSTEM_LIBS={'libc.so','libm.so','libdl.so','liblog.so','libandroid.so'}

def elf_strings(data):
    if data[:5]!=b'\x7fELF\x02' or data[5]!=1:raise ValueError('Expected little-endian ELF64')
    phoff=struct.unpack_from('<Q',data,32)[0];entry_size,count=struct.unpack_from('<HH',data,54)
    loads=[];dynamic=None
    for i in range(count):
        kind,flags,offset,va,pa,size,mem,alignment=struct.unpack_from('<IIQQQQQQ',data,phoff+i*entry_size)
        if kind==1:loads.append((va,offset,size))
        if kind==2:dynamic=(offset,size)
    if dynamic is None:raise ValueError('ELF has no dynamic table')
    entries=[];strings=None
    for cursor in range(dynamic[0],sum(dynamic),16):
        tag,value=struct.unpack_from('<QQ',data,cursor)
        if tag==0:break
        entries.append((tag,value))
        if tag==5:strings=value
    if strings is None:raise ValueError('ELF has no string table')
    base=next(offset+strings-va for va,offset,size in loads if va<=strings<va+size)
    result=[]
    for tag,value in entries:
        if tag in (1,14,15,29):
            start=base+value;end=data.index(0,start);result.append((tag,start,data[start:end].decode()))
    return result

def relocate(data):
    data=bytearray(data);dependencies=[]
    for tag,start,name in elf_strings(data):
        if tag in (15,29):replacement='$ORIGIN'
        else:
            replacement=re.sub(r'\.so\.[0-9.]+$','.so',name)
            if tag==1:dependencies.append((name,replacement))
        if len(replacement)>len(name):raise ValueError('Relocation would grow an ELF string')
        data[start:start+len(name)]=replacement.encode()+b'\0'*(len(name)-len(replacement))
    return bytes(data),dependencies

def extract_deb(path,destination):
    raw=path.read_bytes()
    if raw[:8]!=b'!<arch>\n':raise ValueError('Invalid Debian archive')
    cursor=8
    while cursor+60<=len(raw):
        header=raw[cursor:cursor+60];size=int(header[48:58].strip());name=header[:16].decode().strip().rstrip('/');cursor+=60
        if name.startswith('data.tar.'):
            with tarfile.open(fileobj=io.BytesIO(raw[cursor:cursor+size])) as archive:archive.extractall(destination,filter='data')
            return
        cursor+=size+(size%2)
    raise ValueError('Debian archive has no data payload')

def prepare(sdk,build,assets,native):
    cache=ROOT/'build/android-aot-tools';debs=cache/'debs';debs.mkdir(parents=True,exist_ok=True)
    lock=json.loads((ROOT/'ports/android/compiler-packages.json').read_text())
    def download(package):
        path=debs/Path(package['Filename']).name
        if not path.exists() or hashlib.sha256(path.read_bytes()).hexdigest()!=package['SHA256']:
            subprocess.run(['curl','--fail','--location','--silent','--show-error',lock['base_url']+package['Filename'],'-o',str(path)],check=True)
        if hashlib.sha256(path.read_bytes()).hexdigest()!=package['SHA256']:raise ValueError('Compiler package checksum mismatch')
        return path
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as workers:packages=list(workers.map(download,lock['packages']))
    unpacked=cache/'verified-packages';stamp=unpacked/'manifest.sha256';lock_hash=hashlib.sha256(json.dumps(lock,sort_keys=True).encode()).hexdigest()
    if not stamp.exists() or stamp.read_text()!=lock_hash:
        if unpacked.exists():shutil.rmtree(unpacked)
        unpacked.mkdir()
        for package in packages:extract_deb(package,unpacked)
        stamp.write_text(lock_hash)
    prefix=unpacked/'data/data/com.termux/files/usr'
    native.mkdir(parents=True,exist_ok=True)
    queue=[(prefix/'bin/clang-21','librefare-clang.so'),(prefix/'bin/ld.lld','librefare-lld.so')];copied=set()
    while queue:
        source,name=queue.pop()
        if name in copied:continue
        data,dependencies=relocate(source.resolve().read_bytes());(native/name).write_bytes(data);copied.add(name)
        for original,renamed in dependencies:
            if original in SYSTEM_LIBS:continue
            source=prefix/'lib'/original
            if not source.exists():raise ValueError('Unbundled compiler dependency: '+original)
            queue.append((source,renamed))
    ndk=sdk/'ndk/27.3.13750724'
    llvm=ndk/'toolchains/llvm/prebuilt/darwin-x86_64'
    dolrecomp=build/'moderngekko/vendor/dolphin/DolRecomp/dolrecomp'
    if not dolrecomp.is_file():raise ValueError('Build the Android dolrecomp target first')
    shutil.copy2(dolrecomp,native/'librefare-dolrecomp.so')
    compiler=build/'compiler-assets'
    if compiler.exists():shutil.rmtree(compiler)
    compiler.mkdir()
    include=llvm/'sysroot/usr/include';target=compiler/'sysroot/usr/include';target.mkdir(parents=True)
    for path in include.iterdir():
        if path.name=='c++':continue
        if path.is_dir():shutil.copytree(path,target/path.name)
        else:shutil.copy2(path,target/path.name)
    libs=compiler/'sysroot/usr/lib/aarch64-linux-android/26';libs.mkdir(parents=True)
    for name in ['libc.so','libm.so','libdl.so','crtbegin_so.o','crtend_so.o']:shutil.copy2(llvm/'sysroot/usr/lib/aarch64-linux-android/26'/name,libs/name)
    shutil.copytree(prefix/'lib/clang/21/include',compiler/'clang/include')
    shutil.copy2(llvm/'lib/clang/18/lib/linux/libclang_rt.builtins-aarch64-android.a',compiler/'builtins.a')
    dolphin=ROOT/'lib/ModernGekko/vendor/dolphin';module=compiler/'module';module.mkdir()
    for name in ['module_export.c','module.exports']:shutil.copy2(dolphin/'module-template'/name,module/name)
    shutil.copytree(dolphin/'GXRuntime/include',module/'GXRuntime/include')
    source=module/'GXRuntime/src/core';source.mkdir(parents=True)
    for name in ['cpu.c','cpu_exception.c','cpu_interpreter.c','cpu_interpreter_table.c','cpu_interpreter_float.c','cpu_interpreter_integer.c']:shutil.copy2(dolphin/'GXRuntime/src/core'/name,source/name)
    for header in (dolphin/'GXRuntime/src/core').glob('*.h'):shutil.copy2(header,source/header.name)
    abi=module/'abi';abi.mkdir();shutil.copy2(dolphin/'Source/Core/Core/PowerPC/StaticRecomp/StaticRecompABI.h',abi/'StaticRecompABI.h')
    notices=assets/'licenses';notices.mkdir(exist_ok=True)
    shutil.copy2(ROOT/'LICENSE',notices/'ReFare-GPL-3.0.txt');shutil.copy2(dolphin/'COPYING',notices/'Dolphin-COPYING.txt')
    shutil.copy2(dolphin/'DolRecomp/LICENSE',notices/'DolRecomp-LICENSE.txt')
    for name in ['NOTICE','NOTICE.toolchain']:
        shutil.copy2(ndk/name,notices/('Android-NDK-'+name+'.txt'))
    shutil.copy2(dolphin/'GXRuntime/LICENSE',notices/'GXRuntime-LICENSE.txt');shutil.copy2(dolphin/'GXRuntime/THIRD_PARTY.md',notices/'GXRuntime-THIRD-PARTY.md')
    shutil.copy2(ROOT/'ports/android/compiler-packages.json',notices/'compiler-packages.json')
    doc=prefix/'share/doc'
    if doc.exists():shutil.copytree(doc,notices/'compiler',dirs_exist_ok=True,ignore_dangling_symlinks=True)
    if (prefix/'share/LICENSES').exists():shutil.copytree(prefix/'share/LICENSES',notices/'compiler-LICENSES',dirs_exist_ok=True)
    manifest=[]
    tool_id=hashlib.sha256(b'ReFare Android local AOT pipeline v1\n')
    for directory in [native,compiler]:
        for path in sorted(directory.rglob('*')):
            if path.is_file():
                digest=hashlib.sha256(path.read_bytes()).digest();relative=str(path.relative_to(directory));manifest.append({'file':directory.name+'/'+relative,'sha256':digest.hex()});tool_id.update(relative.encode());tool_id.update(digest)
    assets.mkdir(parents=True,exist_ok=True)
    (assets/'compiler-id.txt').write_text(tool_id.hexdigest())
    (assets/'compiler-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    with zipfile.ZipFile(assets/'compiler.zip','w',zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(compiler.rglob('*')):
            if path.is_file():archive.write(path,str(path.relative_to(compiler)))
    print('Bundled native compiler:',len(copied)+1,'executables/libraries; ID',tool_id.hexdigest())
    return tool_id.hexdigest()
