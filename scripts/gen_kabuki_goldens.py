# Independent reference port of Emu's Kabuki byte-decode (cps1.c ~2851-2905),
# used to generate golden tuples that cross-check the C++ port.
def bitswap1(src, key, select):
    if select & (1 << ((key >> 12) & 7)): src = (src & 0x3F) | ((src & 0x40) << 1) | ((src & 0x80) >> 1)
    if select & (1 << ((key >> 8) & 7)):  src = (src & 0xCF) | ((src & 0x10) << 1) | ((src & 0x20) >> 1)
    if select & (1 << ((key >> 4) & 7)):  src = (src & 0xF3) | ((src & 0x04) << 1) | ((src & 0x08) >> 1)
    if select & (1 << ((key >> 0) & 7)):  src = (src & 0xFC) | ((src & 0x01) << 1) | ((src & 0x02) >> 1)
    return src & 0xFF

def bitswap2(src, key, select):
    if select & (1 << ((key >> 0) & 7)):  src = (src & 0x3F) | ((src & 0x40) << 1) | ((src & 0x80) >> 1)
    if select & (1 << ((key >> 4) & 7)):  src = (src & 0xCF) | ((src & 0x10) << 1) | ((src & 0x20) >> 1)
    if select & (1 << ((key >> 8) & 7)):  src = (src & 0xF3) | ((src & 0x04) << 1) | ((src & 0x08) >> 1)
    if select & (1 << ((key >> 12) & 7)): src = (src & 0xFC) | ((src & 0x01) << 1) | ((src & 0x02) >> 1)
    return src & 0xFF

def rol1(src):
    return (((src & 0x7F) << 1) | ((src & 0x80) >> 7)) & 0xFF

def bytedecode(src, swap1, swap2, xor, select):
    src = bitswap1(src, swap1 & 0xFFFF, select & 0xFF)
    src = rol1(src)
    src = bitswap2(src, swap1 >> 16, select & 0xFF)
    src ^= xor
    src = rol1(src)
    src = bitswap2(src, swap2 & 0xFFFF, (select >> 8) & 0xFF)
    src = rol1(src)
    src = bitswap1(src, swap2 >> 16, (select >> 8) & 0xFF)
    return src & 0xFF

KEYS = {
    'dino':     (0x76543210, 0x24601357, 0x4343, 0x43),
    'punisher': (0x67452103, 0x75316024, 0x2222, 0x22),
    'wof':      (0x01234567, 0x54163072, 0x5151, 0x51),
}

def decode(game, addr, byte):
    s1, s2, ak, xk = KEYS[game]
    op_sel = (addr + ak) & 0xFFFF
    dt_sel = ((addr ^ 0x1FC0) + ak + 1) & 0xFFFF
    return bytedecode(byte, s1, s2, xk, op_sel), bytedecode(byte, s1, s2, xk, dt_sel)

for game in KEYS:
    for addr, byte in [(0x0000, 0x5A), (0x1234, 0xFF), (0x0001, 0x00)]:
        op, dt = decode(game, addr, byte)
        print(f"{game:9} addr={addr:#06x} byte={byte:#04x} -> opcode={op:#04x} data={dt:#04x}")
