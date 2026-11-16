#!/usr/bin/env python3
"""Check external asset precedence and rejection of incomplete resource sets."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', nargs='?', default=str(ROOT / 'captain-comic'))
    args = parser.parse_args()
    binary = str(Path(args.binary).resolve())
    checks = 0
    with tempfile.TemporaryDirectory(prefix='cc-resources-') as temporary:
        tmp = Path(temporary)
        assets = tmp / 'assets'
        shutil.copytree(ROOT / 'assets', assets)

        def run(source, valid, screenshot=None):
            nonlocal checks
            command = [binary, '--assets', str(source), '--headless', '--frames', '1', '--intro-page', '1']
            if screenshot:
                command += ['--screenshot', str(screenshot)]
            result = subprocess.run(command, cwd=tmp, capture_output=True, timeout=30)
            if (result.returncode == 0) != valid:
                raise RuntimeError(f'{source}: exit {result.returncode}\n{result.stderr.decode(errors="replace")}')
            if result.returncode not in (0, 1):
                raise RuntimeError(f'Abnormal termination: {result.returncode}')
            if b'Sanitizer' in result.stderr or b'runtime error:' in result.stderr:
                raise RuntimeError(result.stderr.decode(errors='replace'))
            checks += 1

        first = tmp / 'first.bmp'
        second = tmp / 'second.bmp'
        run(assets, True, first)
        shutil.copy2(assets / 'intro/ending.png', assets / 'intro/title.png')
        run(assets, True, second)
        if first.read_bytes() == second.read_bytes():
            raise RuntimeError('Explicit --assets did not change the title screen')
        # The same resources must load with lower, upper and alternating case paths.
        def mixed_case(name):
            return ''.join(ch.upper() if i % 2 else ch.lower() for i, ch in enumerate(name))

        for label, transform in [('lower', str.lower), ('upper', str.upper), ('mixed', mixed_case)]:
            archive = tmp / f'{label}.pak'
            with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as output:
                for path in assets.rglob('*'):
                    if path.is_file():
                        output.write(path, transform(path.relative_to(tmp).as_posix()))
            third = tmp / f'{label}.bmp'
            run(archive, True, third)
            if second.read_bytes() != third.read_bytes():
                raise RuntimeError(f'{label}-case archive renders different resources')
        mixed_assets = tmp / 'mixed-assets'
        shutil.copytree(assets, mixed_assets)
        for path in sorted(mixed_assets.rglob('*'), key=lambda p: len(p.parts), reverse=True):
            path.rename(path.with_name(mixed_case(path.name)))
        mixed_shot = tmp / 'mixed-directory.bmp'
        run(mixed_assets, True, mixed_shot)
        if second.read_bytes() != mixed_shot.read_bytes():
            raise RuntimeError('Mixed-case directory renders different resources')
        run(tmp / 'missing', False)
        for relative, replacement in [('dos/forest0.pt', b'bad'),
                                       ('dos/forest.tt2', b'bad'),
                                       ('dos/player.spr', b'bad'),
                                       ('dos/world.json', b'{}')]:
            path = assets / relative
            original = path.read_bytes()
            path.write_bytes(replacement)
            run(assets, False)
            path.write_bytes(original)
        print(f'Resource checks: {checks} passed (directory, archive, override, invalid inputs)')


if __name__ == '__main__':
    main()
