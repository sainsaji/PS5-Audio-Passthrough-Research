"""Map a PS5 ELF's PLT stubs to import NIDs, and find call sites of given NIDs.

usage: plt2.py <elf> <nid> [<nid> ...]
Prints, per NID: the PLT stub vaddr and every `call <stub>` site in executable
segments (direct E8 rel32 calls).
"""
import struct, sys

d = open(sys.argv[1], 'rb').read()
want = sys.argv[2:]
phoff = struct.unpack_from('<Q', d, 0x20)[0]
phnum = struct.unpack_from('<H', d, 0x38)[0]
segs, dyn = [], None
for i in range(phnum):
    t, fl, off, va, pa, fsz, msz, al = struct.unpack_from('<IIQQQQQQ', d, phoff + i * 56)
    segs.append((t, fl, off, va, fsz))
    if t == 2:
        dyn = (off, fsz)

def v2o(va):
    for t, fl, off, sva, fsz in segs:
        if t == 1 and sva <= va < sva + fsz:
            return off + va - sva
    # dynlib data segment (0x61000000) and others: try every segment
    for t, fl, off, sva, fsz in segs:
        if sva <= va < sva + fsz:
            return off + va - sva
    raise KeyError(hex(va))

tags = {}
for o in range(dyn[0], dyn[0] + dyn[1], 16):
    t, v = struct.unpack_from('<qQ', d, o)
    tags.setdefault(t, v)
jmprel, pltsz = tags[0x17], tags[0x2]
symtab, strtab = tags[0x6], tags[0x5]
# relocation entries carry the GOT slot; the stub doing `jmp *slot(%rip)` is found by scanning
slot_to_nid = {}
for i in range(pltsz // 24):
    r_off, r_info, _ = struct.unpack_from('<QQq', d, v2o(jmprel) + i * 24)
    no = struct.unpack_from('<I', d, v2o(symtab) + (r_info >> 32) * 24)[0]
    name = d[v2o(strtab) + no:].split(b'\0')[0].decode()
    slot_to_nid[r_off] = name.split('#')[0]

stubs = {}
for t, fl, off, va, fsz in segs:
    if t != 1 or not (fl & 1):
        continue
    blob = d[off:off + fsz]
    i = blob.find(b'\xff\x25')
    while i >= 0:
        rel = struct.unpack_from('<i', blob, i + 2)[0]
        slot = va + i + 6 + rel
        if slot in slot_to_nid:
            stubs[va + i] = slot_to_nid[slot]
        i = blob.find(b'\xff\x25', i + 1)

for nid in want:
    sv = [s for s, n in stubs.items() if n == nid]
    print(nid, 'stubs', [hex(s) for s in sv])
    for t, fl, off, va, fsz in segs:
        if t != 1 or not (fl & 1):
            continue
        blob = d[off:off + fsz]
        i = blob.find(b'\xe8')
        while i >= 0:
            if i + 5 <= len(blob):
                tgt = va + i + 5 + struct.unpack_from('<i', blob, i + 1)[0]
                if tgt in sv:
                    print('   call at', hex(va + i), 'file', hex(off + i))
            i = blob.find(b'\xe8', i + 1)
