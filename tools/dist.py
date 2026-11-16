#!/usr/bin/env python3
"""Package release binaries without deleting unrelated files in dist/."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent


def stage_release(stage, name, binary, windows=False, external=False):
    target = stage / name
    target.mkdir()
    dest = target / ('captain-comic.exe' if windows else 'captain-comic')
    shutil.copy2(ROOT / binary, dest)
    dest.chmod(0o755)
    shutil.copytree(ROOT / 'doc', target / 'doc')
    if external:
        (target / 'data').mkdir()
        shutil.copy2(ROOT / 'data/assets.pak', target / 'data/assets.pak')
        if windows:
            for dll in sorted((ROOT / 'deps/win32/sysroot/bin').glob('*.dll')):
                shutil.copy2(dll, target / 'data' / dll.name)
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--windows-format', choices=('zip', '7z'), default='zip')
    args = parser.parse_args()
    sevenzip = None
    if args.windows_format == '7z':
        sevenzip = shutil.which('7z') or shutil.which('7zz') or shutil.which('7za')
        if not sevenzip:
            parser.error('7z, 7zz or 7za is required')
    dist = ROOT / 'dist'
    dist.mkdir(exist_ok=True)
    variants = [
        ('captain-comic-linux-x64', 'captain-comic', False, True),
        ('captain-comic-linux-x64-standalone', 'captain-comic-standalone', False, False),
        ('captain-comic-linux-x64-static', 'captain-comic-standalone', False, False),
        ('captain-comic-win64', 'captain-comic.exe', True, True),
        ('captain-comic-win64-standalone', 'captain-comic-standalone.exe', True, False),
    ]
    with tempfile.TemporaryDirectory(prefix='_stage-', dir=dist) as temporary:
        stage = Path(temporary)
        for name, binary, windows, external in variants:
            target = stage_release(stage, name, binary, windows, external)
            suffix = '.' + args.windows_format if windows else '.tar.gz'
            archive = stage / (name + suffix)
            if not windows:
                with tarfile.open(archive, 'w:gz') as output:
                    output.add(target, arcname=name)
            elif args.windows_format == 'zip':
                with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as output:
                    for path in sorted(target.rglob('*')):
                        if path.is_file():
                            output.write(path, path.relative_to(stage))
            else:
                subprocess.run([sevenzip, 'a', '-t7z', '-mx=9', str(archive), name], cwd=stage, check=True)
            archive.replace(dist / archive.name)
            print(dist / archive.name)


if __name__ == '__main__':
    main()
