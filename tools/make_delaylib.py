#!/usr/bin/env python3
import subprocess
import sys

dll_path, def_path, dll_name = sys.argv[1:4]
objdump = sys.argv[4] if len(sys.argv) > 4 else "x86_64-w64-mingw32-objdump"
out = subprocess.check_output([objdump, "-p", dll_path], text=True)
tbl = out.split("[Ordinal/Name Pointer] Table")[1].split("\n\n")[0]
names = [line.split()[-1] for line in tbl.splitlines() if "]" in line]

with open(def_path, "w", encoding="utf-8") as f:
    f.write(f"LIBRARY {dll_name}\nEXPORTS\n")
    for name in names:
        f.write(f"  {name}\n")
