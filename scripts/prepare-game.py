#!/usr/bin/env python3
"""Stage a revision-checked game copy; never patch the personal extraction."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct


def patch_dol(data: bytes, manifest: dict, skip_startup: bool) -> bytes:
    if hashlib.sha256(data).hexdigest() != manifest['dol_sha256']:
        raise ValueError('Unsupported main.dol revision; refusing to patch')
    output = bytearray(data)
    if skip_startup:
        for patch in manifest['patches']:
            address = int(patch['address'], 16)
            offset = None
            for index in range(18):
                start = struct.unpack_from('>I', data, 0x48+index*4)[0]
                size = struct.unpack_from('>I', data, 0x90+index*4)[0]
                if start <= address and address+4 <= start+size:
                    offset = struct.unpack_from('>I', data, index*4)[0]+address-start
                    break
            if offset is None or offset+4 > len(data):
                raise ValueError(f'Patch address outside DOL: {address:#x}')
            expected = int(patch['expected'], 16)
            if struct.unpack_from('>I', data, offset)[0] != expected:
                raise ValueError(f'Unexpected instruction at {address:#x}')
            struct.pack_into('>I', output, offset, int(patch['replacement'], 16))
    return bytes(output)


def prepare(source: Path, destination: Path, skip_startup: bool) -> None:
    source = source.resolve()
    destination = destination.resolve()
    if destination == source or source in destination.parents or destination in source.parents:
        raise ValueError('Staging directory must be separate from the extraction')
    manifest = json.loads((Path(__file__).resolve().parent.parent/'config/startup-patches.json').read_text())
    boot = (source/'sys/boot.bin').read_bytes()
    if boot[:6].decode() != manifest['disc_id']:
        raise ValueError('Unsupported disc ID')
    patched = patch_dol((source/'sys/main.dol').read_bytes(),manifest,skip_startup)
    (destination/'sys').mkdir(parents=True,exist_ok=True)
    for item in (source/'sys').iterdir():
        if item.is_file() and item.name != 'main.dol':
            shutil.copy2(item,destination/'sys'/item.name)
    dol = destination/'sys/main.dol'
    if not dol.exists() or dol.read_bytes() != patched:
        dol.write_bytes(patched)
    assets = destination/'files'
    if assets.is_symlink():
        if assets.resolve() != source/'files':
            raise ValueError('Staging assets point to a different extraction')
    elif not assets.exists():
        try:
            assets.symlink_to(source/'files',target_is_directory=True)
        except OSError:
            shutil.copytree(source/'files',assets)
    print(f'Staged {destination}; startup skips {"on" if skip_startup else "off"}; original DOL unchanged')


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    parser.add_argument('destination',type=Path)
    parser.add_argument('--skip-startup',choices=('0','1'),default='1')
    args=parser.parse_args()
    try:
        prepare(args.source,args.destination,args.skip_startup=='1')
    except (OSError,ValueError) as error:
        parser.exit(1,f'error: {error}\n')
