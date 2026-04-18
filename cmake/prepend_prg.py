#!/usr/bin/env python3
# prepend_prg.py  lo hi input output
# Prepend a 2-byte load-address header (little-endian) to a raw binary.
import sys
lo, hi = int(sys.argv[1], 16), int(sys.argv[2], 16)
data = open(sys.argv[3], 'rb').read()
open(sys.argv[4], 'wb').write(bytes([lo, hi]) + data)
