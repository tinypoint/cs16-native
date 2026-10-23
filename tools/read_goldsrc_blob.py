#!/usr/bin/env python3
"""Decode a legacy GoldSrc BLOB into a bounded analysis image, not a new engine.

Format reference: https://github.com/oxiKKK/goldsource-dll-decryptor
We retain section addresses and code bytes; no guessed PE/export headers are
written. Inputs remain untouched. Output is for static analysis, not execution.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

def decode(path):
    raw = path.read_bytes()
    if len(raw) < 92 or struct.unpack_from('<I', raw, 64)[0] != 0x12345678:
        raise ValueError('Not a classic GoldSrc BLOB')
    data = bytearray(raw); key = 0x57
    for i in range(68, len(data)):
        data[i] ^= key
        key = (key + data[i] + 0x57) & 255
    random, count_minus_one, interface, base, entry, imports = struct.unpack_from('<6I', data, 68)
    base ^= 0x49c042d1; interface ^= 0x7a32bc85
    entry = (entry - 12) & 0xffffffff; imports ^= 0x872c3d47
    count = count_minus_one + 1
    if not 1 <= count <= 64 or 92 + count * 20 > len(data):
        raise ValueError('Invalid section count')
    sections = []
    for i in range(count):
        va, memory_size, file_size, offset, special = struct.unpack_from('<4IB3x', data, 92 + 20 * i)
        if va < base or file_size > memory_size or offset + file_size > len(data) or va + memory_size > 2**32:
            raise ValueError('Invalid section bounds')
        sections.append({'address': va, 'memoryBytes': memory_size, 'fileBytes': file_size,
                         'fileOffset': offset, 'special': special})
    size = max(s['address'] + s['memoryBytes'] - base for s in sections)
    if size > 256 * 1024 * 1024:
        raise ValueError('Image too large')
    image = bytearray(size)
    ranges = []
    for s in sections:
        start = s['address'] - base; end = start + s['memoryBytes']
        if any(start < b and end > a for a, b in ranges):
            raise ValueError('Overlapping sections')
        ranges.append((start, end)); image[start:start + s['fileBytes']] = data[s['fileOffset']:s['fileOffset'] + s['fileBytes']]
    def rva(address):
        offset = address - base
        if not any(a <= offset < b for a, b in ranges):
            raise ValueError('Address outside image sections')
        return offset
    def u32(offset):
        if offset < 0 or offset + 4 > len(image):
            raise ValueError('Invalid DWORD address')
        return struct.unpack_from('<I', image, offset)[0]
    def string(offset):
        if offset < 0 or offset >= len(image):
            raise ValueError('Invalid string address')
        end = image.find(0, offset, min(offset + 4096, len(image)))
        if end < 0:
            raise ValueError('Unterminated string')
        return image[offset:end].decode('ascii')
    rva(entry); rva(interface)
    import_list = []
    desc = rva(imports)
    for n in range(1024):
        pos = desc + n * 20
        if pos + 20 > len(image):
            raise ValueError('Import descriptors outside image')
        original, timestamp, forward, name, first = struct.unpack_from('<5I', image, pos)
        if not any((original, timestamp, forward, name, first)):
            break
        dll = string(name); thunk = original or first
        for index in range(65536):
            item = u32(thunk + index * 4)
            if not item:
                break
            symbol = '#' + str(item & 0xffff) if item & 0x80000000 else string(item + 2)
            import_list.append({'dll': dll, 'name': symbol, 'iatAddress': base + first + index * 4})
        else:
            raise ValueError('Unterminated import thunks')
    else:
        raise ValueError('Unterminated import descriptors')
    return image, {'source': str(path), 'inputSha256': hashlib.sha256(raw).hexdigest(),
                   'format': 'GoldSrc classic BLOB', 'base': base, 'entry': entry,
                   'interfaceAddress': interface, 'importDirectoryAddress': imports,
                   'imageBytes': len(image), 'sections': sections, 'imports': import_list,
                   'codeBytesChanged': False, 'programExecuted': False,
                   'exportsReconstructed': False}

if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('input', type=Path); ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    image, metadata = decode(args.input)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'image.bin').write_bytes(image)
    (args.output / 'module.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(json.dumps({k: metadata[k] for k in ('format', 'base', 'entry', 'imageBytes')}))
    print('Sections:', len(metadata['sections']), 'imports:', len(metadata['imports']))
