#!/usr/bin/env python3
"""Optional development tool: extract original Revision 5 effects without running DOS code.
Usage: python3 tools/extract_effects.py COMIC.EXE assets/dos
Python is not required for running the game.
"""
from pathlib import Path
import struct
import sys

def unpack_exepack(exe):
    if len(exe) < 28 or exe[:2] != b'MZ': raise ValueError('Expected MZ executable')
    h = struct.unpack('<14H', exe[:28]); start = h[4] * 16; pack = start + h[11] * 16
    header = struct.unpack('<9H', exe[pack:pack + 18])
    if header[8] != 0x4252: raise ValueError('Expected Microsoft EXEPACK RB header')
    data = exe[start:pack]; i = len(data) - 1; chunks = []
    while i >= 0 and data[i] == 255: i -= 1
    while i >= 2:
        op = data[i]; i -= 1
        if op & 0xfe not in (0xb0, 0xb2): raise ValueError('Invalid EXEPACK opcode')
        length = int.from_bytes(data[i - 1:i + 1], 'little'); i -= 2
        if op & 0xfe == 0xb0:
            if i < 0: raise ValueError('Truncated EXEPACK run')
            chunk = bytes([data[i]]) * length; i -= 1
        else:
            if length > i + 1: raise ValueError('Truncated EXEPACK literal')
            chunk = data[i - length + 1:i + 1]; i -= length
        chunks.append(chunk)
        if op & 1: break
    else: raise ValueError('Missing EXEPACK terminator')
    image = data[:i + 1] + b''.join(reversed(chunks))
    if len(image) != header[6] * 16: raise ValueError('EXEPACK destination length mismatch')
    return image


if __name__ == '__main__':
    if len(sys.argv) != 3: raise SystemExit(__doc__)
    data=unpack_exepack(Path(sys.argv[1]).read_bytes())
    if len(data)!=100208 or data[0xfdf0:0xfdf8]!=b'lake.tt2':
        raise SystemExit('Expected the supplied Captain Comic Revision 5')
    output=Path(sys.argv[2]);output.mkdir(parents=True,exist_ok=True)
    (output/'fireball.spr').write_bytes(data[0xa980:0xaa20])
    (output/'enemy_hit.spr').write_bytes(data[0xaa20:0xac00])
    (output/'death.spr').write_bytes(data[0xade0:0xb7e0])
    (output/'teleport.spr').write_bytes(data[0xb7e0:0xbba0])
    (output/'arrival.spr').write_bytes(data[0xbba0:0xcaa0])
