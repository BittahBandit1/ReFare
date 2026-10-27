#!/usr/bin/env python3
"""Apply ReFare's video defaults while preserving other runtime settings."""
import argparse
import configparser
from pathlib import Path
import shutil


def configure(user_directory: Path, aspect: str) -> Path:
    preset = Path(__file__).resolve().parent.parent / 'config' / 'GFX.ini'
    defaults = configparser.ConfigParser(interpolation=None)
    defaults.optionxform = str
    defaults.read(preset)
    destination = user_directory / 'Config' / 'GFX.ini'
    destination.parent.mkdir(parents=True, exist_ok=True)
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str
    if destination.exists():
        with destination.open() as source:
            config.read_file(source)
        backup = destination.with_suffix('.ini.before-refare')
        if not backup.exists():
            shutil.copy2(destination, backup)
    if not config.has_section('Settings'):
        config.add_section('Settings')
    for key, value in defaults.items('Settings'):
        config.set('Settings', key, value)
    if aspect == 'original':
        config.set('Settings', 'AspectRatio', '2')
        config.set('Settings', 'wideScreenHack', 'False')
    with destination.open('w') as output:
        config.write(output)
    return destination


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--user-dir', type=Path, default=Path('build/user'))
    parser.add_argument('--aspect', choices=('window', 'original'), default='window')
    args = parser.parse_args()
    print(f'Aspect mode: {args.aspect}; settings: {configure(args.user_dir, args.aspect)}')
