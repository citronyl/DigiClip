# pack_barks.py   usage: python pack_barks.py bark1.raw bark2.raw ...
# Builds one flash image (sector 0 = clip table, each clip on its own 4 KB sector)
# and splits it into chunks/chunk_N.h, one per loader upload (24 KB each).
import sys, struct, pathlib
SECTOR, CHUNK = 4096, 24576
clips = [pathlib.Path(p).read_bytes() for p in sys.argv[1:]]
assert 1 <= len(clips) <= 32, "1 to 32 clips"
table = bytearray(b"SEAL") + struct.pack("<H", len(clips))
img = bytearray(SECTOR)
for c in clips:
    table += struct.pack("<II", len(img), len(c))
    img += c
    img += b"\x80" * (-len(img) % SECTOR)
img[0:len(table)] = table
out = pathlib.Path("chunks"); out.mkdir(exist_ok=True)
for old in out.glob("chunk_*.h"): old.unlink()
n = (len(img) + CHUNK - 1) // CHUNK
for i in range(n):
    part = img[i*CHUNK:(i+1)*CHUNK]
    (out / f"chunk_{i}.h").write_text(
        "#pragma once\n#include <avr/pgmspace.h>\n"
        f"#define CHUNK_INDEX {i}\n#define CHUNK_OFFSET {i*CHUNK}UL\n#define CHUNK_LEN {len(part)}U\n"
        f"const uint8_t chunk[CHUNK_LEN] PROGMEM = {{{','.join(map(str, part))}}};\n")
for k, c in enumerate(clips):
    print(f"clip {k}: {len(c)} bytes = {len(c)/7812:.2f} s")
print(f"image {len(img)} bytes -> {n} chunks in chunks/ (one upload each)")