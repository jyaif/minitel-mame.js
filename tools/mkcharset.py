#!/usr/bin/env python3
"""Turn the TS9347 character generator ROM into a C header for the wasm build.

Usage: mkcharset.py src/ts9347.bin build/charset_rom.h

Run by the Makefile; the header is a build artifact and is not kept in the tree.
"""
import sys

src, dst = sys.argv[1], sys.argv[2]
data = open(src, 'rb').read()
if len(data) != 8192:
    sys.exit("%s must be 8192 bytes (got %d)" % (src, len(data)))

rows = ['\t' + ' '.join('0x%02x,' % b for b in data[i:i + 16])
        for i in range(0, len(data), 16)]

header = '''// license:BSD-3-Clause
//
// GENERATED FILE -- do not edit, and do not commit.
//
// Built by tools/mkcharset.py from %s: the TS9347 character generator ROM,
// which the video chip reads directly and which therefore has to be present
// for anything to be drawn. "make" regenerates it.

#ifndef MINITEL_CHARSET_ROM_H
#define MINITEL_CHARSET_ROM_H

#include "types.h"

// ts9347.bin  CRC(acff72e7) SHA1(54c8b6f5b6407f13a933a40b5b7742ca06cdc1a3)
static const u8 charset_rom[8192] = {
''' % src

footer = '''
};

#endif // MINITEL_CHARSET_ROM_H
'''

open(dst, 'w').write(header + '\n'.join(rows) + footer)
