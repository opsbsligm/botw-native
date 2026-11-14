"""Keep the explicit SDK7 example coupled to the actual fixed-size initializer."""
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from code_mods import parse_pchtxt


class Sdk7MemoryMod(unittest.TestCase):
    def test_example_expands_application_root_and_texture_heaps_together(self):
        text = (ROOT / 'examples/mods/20-sdk7-paired-256/exefs/pool-256-heap-3280.pchtxt').read_text()
        patches = parse_pchtxt(text, '8e9978d50bdd20b4c8395a106c27ffde')
        # Startup/constructor traces found independent W0 and W21 heap limits.
        self.assertEqual([p.offset for p in patches], [0xe537dc, 0xe540a0, 0x1804d44])
        values = []
        for patch, register in zip(patches, [0, 21, 8]):
            self.assertEqual(len(patch.data), 4)
            word, = struct.unpack('<I', patch.data)
            self.assertEqual(word & 0xff80001f, 0x52800000 | register)
            self.assertEqual((word >> 21) & 3, 1)
            values.append(((word >> 5) & 0xffff) << 16)
        heap, root, pool = values
        self.assertEqual(heap, (3120 + 160) * 2**20)
        self.assertEqual(root, (3088 + 160) * 2**20)
        self.assertEqual(root + 32 * 2**20, heap)
        self.assertEqual(pool, 256 * 2**20)
        self.assertLessEqual(heap, 3328 * 2**20)
        self.assertLess(heap + 64 * 2**20, (3285 + 160) * 2**20)
