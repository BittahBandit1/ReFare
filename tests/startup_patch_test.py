"""Verify the private disc revision and narrow startup control-flow patches."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

root=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('prepare_game',root/'scripts/prepare-game.py')
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
manifest=json.loads((root/'config/startup-patches.json').read_text())
original=(root/'extracted/Crazy-Taxi-USA/sys/main.dol').read_bytes()
patched=module.patch_dol(original,manifest,True)
assert module.patch_dol(original,manifest,False)==original
assert hashlib.sha256(original).hexdigest()==manifest['dol_sha256']
changed={i for i,(a,b) in enumerate(zip(original,patched)) if a!=b}
allowed=set()
for address in (0x80004420,0x80003fa0):
    allowed.update(range(address-0x80003000,address-0x80003000+4))
assert changed and changed<=allowed
assert struct.unpack_from('>I',patched,0x1420)[0]==0x60000000
branch=struct.unpack_from('>I',patched,0xfa0)[0]
assert branch>>26==18 and branch&3==0
assert 0x80003fa0+(branch&0x03fffffc)==0x800041cc
# The Sega playback block and the common finalization block remain intact.
assert original[0xdfc:0xfa0]==patched[0xdfc:0xfa0]
assert original[0x11cc:0x1248]==patched[0x11cc:0x1248]
try:
    module.patch_dol(original[:-1]+bytes([original[-1]^1]),manifest,True)
except ValueError:
    pass
else:
    raise AssertionError('Unrecognized DOL revision accepted')
wrong=json.loads(json.dumps(manifest)); wrong['patches'][0]['expected']='0x00000000'
try:
    module.patch_dol(original,wrong,True)
except ValueError:
    pass
else:
    raise AssertionError('Unexpected source instruction accepted')
print('PASS: revision guard, expected instructions, two patch sites, branch destination, Sega/finalization preservation, and original-mode restoration')
