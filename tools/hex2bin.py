#!/usr/bin/env python3
"""A C array (xxd -i, as in test/roms/*.h) back to its binary file: hex2bin.py in.h out.bin"""
import re, sys
s = open(sys.argv[1]).read()
body = s[s.index('{') + 1:s.index('}')]
open(sys.argv[2], 'wb').write(bytes(int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]{2}', body)))
