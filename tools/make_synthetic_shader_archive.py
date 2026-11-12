"""Create three original, minimal shaders for asset-free production prewarm regression."""
import argparse
from pathlib import Path
import struct


def create(root):
    root = Path(root)
    if root.exists() or root.is_symlink():
        raise ValueError('Use a new task-owned output directory')
    blob = bytearray(0x2300)
    blob[:4] = b'BNSH'
    blob[0x20:0x24] = b'grsc'
    struct.pack_into('<I', blob, 0x3C, 2)
    struct.pack_into('<Q', blob, 0x40, 0x80)
    for variation, program in [(0x80, 0x120), (0xC0, 0x160)]:
        struct.pack_into('<Q', blob, variation + 0x10, program)
    struct.pack_into('<Q', blob, 0x128, 0x200)  # vertex
    struct.pack_into('<Q', blob, 0x148, 0x240)  # fragment
    struct.pack_into('<Q', blob, 0x190, 0x280)  # compute
    instructions = [0x0100000000000000 | (7 << 16) | register |
                    ((0x3F800000 if register in (0, 3) else 0) << 20) for register in range(4)]
    instructions.append(0xE30000000007000F)
    words = []
    for instruction in instructions:
        if len(words) % 4 == 0:
            words.append(0)  # scheduling word
        words.append(instruction)
    while len(words) < 8:
        words.append(0)
    code = struct.pack('<8Q', *words)
    for stage, descriptor, control, data in [(0, 0x200, 0x400, 0x2000), (1, 0x240, 0xC00, 0x2100), (5, 0x280, 0x1400, 0x2200)]:
        body = bytearray((0x80 if stage != 5 else 0) + len(code))
        if stage != 5:
            struct.pack_into('<I', body, 0, 0x12345678)
            if stage == 1:
                struct.pack_into('<I', body, 0x30 + 0x48, 15)  # one RGBA output
        body[-len(code):] = code
        struct.pack_into('<QQII', blob, descriptor + 8, control, data, len(body), 0x780)
        struct.pack_into('<I', blob, control, 0x98761234)
        struct.pack_into('<I', blob, control + 0x700, len(body))
        struct.pack_into('<3I', blob, control + 0x748, 1, 1, 1)
        blob[data:data + len(body)] = body
    target = root / 'romfs/System/synthetic.bnsh'
    target.parent.mkdir(parents=True)
    target.write_bytes(blob)
    return root


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    print(create(args.out))
