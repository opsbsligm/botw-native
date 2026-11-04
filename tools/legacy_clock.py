"""BOTW 1.6.0 measured clock profile; MIT MacNativeGames f1d00772 source.

VFR, fade, UI, physics solver, held-button repeat and sound-link timers retain
their original game logic while reading a measured 30Hz frame ratio. Every
replaced instruction and its surrounding context are verified before emitting.
"""
import struct

GUARDS = {
    0x1214474: 0x8b0a136a,
    0x1214478: 0x5289400b,
    0x121447c: 0x72a000cb,
    0x1214480: 0x8b0b014b,
    0x1214484: 0xb940016c,
    0x1214488: 0x37f80acc,
    0x121448c: 0x5100058d,
    0x1214490: 0xb900016d,
    0x1214494: 0x35000a6c,
    0x126e490: 0xb9410268,
    0x126e494: 0x7100051f,
    0x126e498: 0xb901166b,
    0x126e49c: 0xb900127f,
    0x126e4a0: 0x5400044b,
    0x126e4a4: 0x2a1f03e8,
    0x126e4a8: 0xaa1f03e9,
    0x126e4ac: 0x320003ea,
    0x126e4b0: 0x14000002,
    0x126e4b4: 0xb941166b,
    0x126e4b8: 0x1ac9214c,
    0x126e4bc: 0x6a0c017f,
    0x126e4c0: 0x54000180,
    0x126e4c4: 0x8b09026e,
    0x126e4c8: 0x394341cd,
    0x126e4cc: 0x8b090a6b,
    0x126e4d0: 0x340001ed,
    0x126e4d4: 0xb940316f,
    0x126e4d8: 0x3942c1ce,
    0x126e4dc: 0x6b0e01ee,
    0x126e4e0: 0x540000e1,
    0x126e4e4: 0x2a0c0108,
    0x126e4e8: 0xb9001268,
    0x126e4ec: 0x14000008,
    0x126e4f0: 0x8b090a6b,
    0x126e4f4: 0xb900317f,
    0x126e4f8: 0x14000008,
    0x126e4fc: 0x54000089,
    0x126e500: 0x1acd09cf,
    0x126e504: 0x1b0db9ed,
    0x126e508: 0x34fffeed,
    0x126e50c: 0xb940316c,
    0x126e510: 0x1100058c,
    0x126e514: 0xb900316c,
    0x126e518: 0xb981026b,
    0x126e51c: 0x91000529,
    0x126e520: 0xeb0b013f,
    0x126e524: 0x54fffc8b,
    0x161847c: 0x1e290908,
    0x1618480: 0xbd400109,
    0x1618484: 0xb94c3680,
    0x1821564: 0xf9400108,
    0x1821568: 0xb9400108,
    0x182156c: 0x7100011f,
    0x1821570: 0x1a9f1508,
    0x1821574: 0x1e230100,
    0x1821578: 0x1e2c100b,
    0x182157c: 0xb0006f68,
    0x1821580: 0xbd409109,
    0x1821584: 0xbd435ee1,
    0x1821588: 0x1e2b0800,
    0x182158c: 0x1e290800,
    0x18522ec: 0xb9400108,
    0x18522f0: 0x7100011f,
    0x18522f4: 0x1a9f1508,
    0x18522f8: 0x1e230100,
    0x18522fc: 0x1e2c1001,
    0x1852300: 0x1e210800,
    0x1852304: 0xbd0b22a0,
    0x1c0120c: 0x1e202020,
    0x1c01210: 0x3900a008,
    0x1c01214: 0x54000060,
    0x1c01218: 0xbd003c01,
    0x1c59fa8: 0xbd400100,
    0x1c59fac: 0x1e2e1001,
    0x1c59fb0: 0x1e214800,
    0x1c5a00c: 0x90008348,
    0x1c5a010: 0xf9401508,
    0x1c5a014: 0xbd008500,
    0x1c5a018: 0x91090300,
    0x1c5a01c: 0x94133c99,
}


def table(module):
    if module.name != 'main' or module.base != 0x7000000000:
        raise ValueError('Clock profile requires exact SDK7 main module')
    for offset, expected in GUARDS.items():
        if struct.unpack_from('<I', module.nso.image, offset)[0] != expected:
            raise ValueError(f'SDK7 clock instruction differs at {offset:#x}')
    base = module.base
    replacements = {}
    for offset in (0x1c0120c, 0x1821568, 0x1821570, 0x1821574, 0x1821588,
                   0x18522ec, 0x18522f4, 0x18522f8, 0x1852300):
        replacements[base + offset] = lambda m, a: '; /* measured ratio replaces interval arithmetic */'
    replacements[base + 0x1c01214] = lambda m, a: 'FP_SETS(1, sdk7_frame_ratio());'
    for offset in (0x182156c, 0x18522f0):
        replacements[base + offset] = lambda m, a: 'FP_SETS(0, sdk7_frame_ratio());'
    replacements[base + 0x1618480] = lambda m, a: 'FP_SETS(9, 1.0f);'
    # Hold counters advance on 30Hz ticks, releases still reset immediately.
    repeat = module.lift(base + 0x126e490 + 21 * 4)
    if repeat.kind != 'op':
        raise ValueError('Unexpected repeat instruction kind')
    replacements[base + 0x126e490 + 21 * 4] = lambda m, a: 'if (sdk7_pad_tick()) { ' + repeat.code + ' }'
    replacements[base + 0x126e490 + 32 * 4] = lambda m, a: 'c->x[12] = (uint32_t)c->x[12] + sdk7_pad_tick();'
    replacements[base + 0x1214474 + 6 * 4] = lambda m, a: 'c->x[13] = (uint32_t)c->x[12] - sdk7_pad_tick();'
    branch = module.lift(base + 0x1214474 + 8 * 4)
    if branch.kind != 'bcond':
        raise ValueError('Unexpected menu countdown branch')
    module.branch_conditions[base + 0x1214474 + 8 * 4] = '(!sdk7_pad_tick() || (' + branch.cond + '))'
    replacements[base + 0x1c5a014] = lambda m, a: 'FP_SETS(0, sdk7_frame_ratio()); ST32(c->x[8] + 0x84, (uint32_t)c->v[0]);'
    return replacements
