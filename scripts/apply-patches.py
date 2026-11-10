#!/usr/bin/env python3
"""Apply the recorded runtime changes on Windows, macOS, and Linux."""
from pathlib import Path
import subprocess
import re
import shutil
import tempfile

ROOT = Path(__file__).resolve().parent.parent
# Keep the ordering in the existing shell entry point as the single source of truth.
def stack_is_applied(directory: Path, patches: list[Path]) -> bool:
    # Later patches can replace lines added by earlier patches. Test the whole
    # reverse stack in a disposable file tree instead of testing stale hunks.
    paths = set()
    for patch in patches:
        paths.update(re.findall(r"^(?:--- a/|\+\+\+ b/)(.+)$", patch.read_text(), re.MULTILINE))
    with tempfile.TemporaryDirectory(prefix='refare-patch-check-') as temporary:
        snapshot = Path(temporary)
        for relative in paths:
            name = Path(relative)
            if name.is_absolute() or '..' in name.parts:
                raise ValueError('Unsafe patch path: ' + relative)
            source = directory / name
            if source.is_file():
                destination = snapshot / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, destination)
        for patch in reversed(patches):
            result = subprocess.run(['git', '-C', str(snapshot), 'apply', '--reverse', str(patch)],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if result.returncode:
                return False
    return True

def apply() -> None:
    recipes = {}
    for line in (ROOT / 'scripts/apply-patches.sh').read_text().splitlines():
        if line.startswith('apply_patch lib/'):
            _, directory, filename = line.split()
            recipes.setdefault(ROOT / directory, []).append(ROOT / 'patches' / filename)
    for directory, patches in recipes.items():
        if stack_is_applied(directory, patches):
            continue
        for patch in patches:
            command = ['git', '-C', str(directory), 'apply']
            if subprocess.run(command + ['--reverse', '--check', str(patch)],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
                continue
            subprocess.run(command + ['--check', str(patch)], check=True)
            subprocess.run(command + [str(patch)], check=True)

if __name__ == '__main__':
    apply()
