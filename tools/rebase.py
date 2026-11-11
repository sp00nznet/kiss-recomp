#!/usr/bin/env python3
"""Map every module KISS Psycho Circus runs to a fixed, non-overlapping VA.

client.exe is the LithTech 2 engine, linked at 0x01CA0000 without
relocations. The rest were linked at 0x10000000 (cshell.dll at 0x20000000) and
relocated by Windows at run time; a static recompilation has every absolute
address baked in, so they are relocated here, once, before disassembly, and
the host maps each copy at exactly this base (g_modules in src/runtime/host.c).
Unlike LithTech 1 (Shogo), the game DLLs sit beside client.exe, not in a .rez.

    py -3 tools/rebase.py            # game/ -> work/rebased/
"""
import os
import sys
import pefile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# module -> base. Keep in step with src/runtime/host.c.
MODULES = {
    'client.exe':  0x01CA0000,   # linked base: no relocations
    'server.dll':  0x10000000,   # linked base
    'd3d.ren':     0x10100000,
    'cres.dll':    0x10300000,
    'object.lto':  0x10400000,
    'sres.dll':    0x10600000,
    'cdaudio.dll': 0x10700000,
    'cshell.dll':  0x20000000,   # linked base
}
# Byte patches applied to the relocated images, before lifting: (module, VA,
# the original bytes, the new ones). Each is checked against the original.
PATCHES = []


def rebase(raw, base):
    """The file's bytes with every HIGHLOW fixup applied for `base` (by hand:
    pefile's relocate_image() also rewrites the import thunks it parsed)."""
    pe = pefile.PE(data=raw)
    data = bytearray(pe.__data__)
    delta = (base - pe.OPTIONAL_HEADER.ImageBase) & 0xFFFFFFFF
    if delta:
        for block in getattr(pe, 'DIRECTORY_ENTRY_BASERELOC', ()):
            for e in block.entries:
                if e.type == 3:                      # IMAGE_REL_BASED_HIGHLOW
                    off = pe.get_offset_from_rva(e.rva)
                    v = int.from_bytes(data[off:off + 4], 'little')
                    data[off:off + 4] = ((v + delta) & 0xFFFFFFFF).to_bytes(4, 'little')
                elif e.type != 0:
                    raise ValueError('relocation type %d' % e.type)
        off = pe.OPTIONAL_HEADER.get_file_offset() + 28   # ImageBase
        data[off:off + 4] = base.to_bytes(4, 'little')
    return bytes(data), pe.OPTIONAL_HEADER.SizeOfImage


def main():
    game = os.path.join(ROOT, 'game')
    out = os.path.join(ROOT, 'work', 'rebased')
    os.makedirs(out, exist_ok=True)
    spans = []
    for name, base in MODULES.items():
        data, size = rebase(open(os.path.join(game, name), 'rb').read(), base)
        data = bytearray(data)
        pe = pefile.PE(data=bytes(data), fast_load=True)
        for mod, va, old, new in PATCHES:
            if mod == name:
                off = pe.get_offset_from_rva(va - base)
                if data[off:off + len(old)] != old:
                    sys.exit(f'{name}: patch at {va:#x} does not match')
                data[off:off + len(new)] = new
        spans.append((base, base + size, name))
        with open(os.path.join(out, name), 'wb') as f:
            f.write(data)
        print(f'{name:12} {base:#010x}-{base + size:#010x}')
    spans.sort()
    for (a0, a1, n), (b0, _, m) in zip(spans, spans[1:]):
        if a1 > b0:
            sys.exit(f'{n} overlaps {m}')


if __name__ == '__main__':
    sys.exit(main())
