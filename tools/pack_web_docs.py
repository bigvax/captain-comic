#!/usr/bin/env python3
"""Copy local documentation and its images into the static web distribution."""
import shutil
import sys
from pathlib import Path


def main():
	root = Path(__file__).resolve().parent.parent
	output = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'build/web'
	output.mkdir(parents=True, exist_ok=True)
	for name in ('readme.html', 'readme.js', 'readme.css'):
		shutil.copyfile(root / 'web' / name, output / name)
	shutil.copytree(root / 'web/vendor', output / 'vendor', dirs_exist_ok=True)
	shutil.copyfile(root / 'README.md', output / 'README.md')
	doc_output = output / 'doc'
	doc_output.mkdir(exist_ok=True)
	for source in (root / 'doc').glob('*.md'):
		shutil.copyfile(source, doc_output / source.name)
	shutil.copytree(root / 'doc/images', doc_output / 'images', dirs_exist_ok=True)
	print(f'Локальная документация: {output / "readme.html"}')


if __name__ == '__main__':
	main()
