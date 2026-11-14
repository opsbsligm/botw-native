"""Native HUD growth must survive a mod's table; test the production FS path without game data."""
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
HUD = 'Layout/Common.blarc'


def table(entries, names=()):
    crcs = sorted((zlib.crc32(k.encode()), v) for k, v in entries.items())
    return (b'RSTB' + struct.pack('<II', len(crcs), len(names))
            + b''.join(struct.pack('<II', k, v) for k, v in crcs)
            + b''.join(k.encode().ljust(128, b'\0') + struct.pack('<I', v) for k, v in names))


def literal(data):
    return (b'Yaz0' + struct.pack('>I', len(data)) + bytes(8)
            + b''.join(b'\xff' + data[i:i + 8] for i in range(0, len(data), 8)))


def values(data):
    nc, nn = struct.unpack_from('<II', data, 4)
    crc = dict(struct.unpack_from('<II', data, 12 + i * 8) for i in range(nc))
    names = {}
    for i in range(nn):
        off = 12 + nc * 8 + i * 132
        names[data[off:off + 128].split(b'\0')[0].decode()] = struct.unpack_from('<I', data, off + 128)[0]
    return crc, names


class RstbBudget(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='rstb-budget-', dir=Path.cwd())
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        fs = (ROOT / 'runtime/services/fs.c').read_text()
        pieces = []
        for name in ('rstb_yaz0', 'rstb_budget'):
            match = re.search(r'// TEST\(' + name + r':begin\)\n(.*?)// TEST\(' + name + r':end\)', fs, re.S)
            assert match, name
            pieces.append(match.group(1))
        (cls.directory / 'rstb_fs_under_test.inc').write_text('\n'.join(pieces))
        cls.exe = cls.directory / 'probe'
        sanitizers = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if sys.platform == 'linux' else []
        compiled = subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', *sanitizers,
                                   '-I', str(cls.directory), '-I', str(ROOT / 'runtime'),
                                   str(ROOT / 'tests/rstb_budget_test.c'), str(ROOT / 'runtime/rstb_budget.c'),
                                   '-lz', '-o', str(cls.exe)], capture_output=True, text=True, timeout=45)
        if compiled.returncode:
            raise RuntimeError(compiled.stdout + compiled.stderr)

    def run_probe(self, original=None, overlay=None, mode='merge', dlc=False, expected=0):
        original = table({HUD: 32710368}) if original is None else original
        source = self.directory / 'original.bin'
        source.write_bytes(original)
        env = dict(os.environ)
        env.pop('TEST_RSTB_OVERLAY', None)
        env.pop('TEST_MERGED_DLC', None)
        if overlay is not None:
            path = self.directory / 'mod.bin'
            path.write_bytes(overlay)
            env['TEST_RSTB_OVERLAY'] = str(path)
        if dlc:
            env['TEST_MERGED_DLC'] = '1'
        result = subprocess.run([str(self.exe), str(source), mode], env=env,
                                capture_output=True, timeout=15)
        self.assertEqual(result.returncode, expected, result.stderr.decode(errors='replace'))
        self.assertEqual(source.read_bytes(), original)
        if overlay is not None:
            self.assertEqual(path.read_bytes(), overlay)
        return result

    def assert_budget(self, data, name, expected):
        crc, names = values(data)
        self.assertEqual(crc.get(zlib.crc32(name.encode()), names.get(name)), expected)

    def test_original_raw_and_yaz0_keep_native_floor(self):
        for original in (table({HUD: 32710368}), literal(table({HUD: 32710368}))):
            with self.subTest(encoded=original[:4]):
                out = self.run_probe(original=original).stdout
                self.assert_budget(out, HUD, 32728448)
                self.assertNotIn(zlib.crc32(b'Voice/stream.bfstm'), values(out)[0])

    def test_mod_smaller_already_compensated_and_larger_budgets(self):
        for budget in (0, 32710368, 32728448, 40000000):
            for encode in (bytes, literal):
                with self.subTest(budget=budget, encoding=encode.__name__):
                    out = self.run_probe(overlay=encode(table({HUD: budget, 'Actor/Mod.bfres': 8000000}))).stdout
                    self.assert_budget(out, HUD, max(budget, 32728448))
                    self.assert_budget(out, 'Actor/Mod.bfres', 8000000)

    def test_missing_native_entry_is_inserted_in_sorted_crc_table(self):
        out = self.run_probe(overlay=table({'a': 123, 'z': 456})).stdout
        self.assert_budget(out, HUD, 32728448)
        self.assert_budget(out, 'a', 123)
        self.assert_budget(out, 'z', 456)
        nc, = struct.unpack_from('<I', out, 4)
        crcs = [struct.unpack_from('<I', out, 12 + i * 8)[0] for i in range(nc)]
        self.assertEqual(crcs, sorted(set(crcs)))

    def test_named_entries_preserved_and_guest_crc_lookup_precedence(self):
        for overlay, want in ((table({}, [(HUD, 40000000), ('Other/Name', 8)]), 40000000),
                              (table({HUD: 32710368}, [(HUD, 50000000)]), 32728448)):
            out = self.run_probe(overlay=overlay).stdout
            self.assert_budget(out, HUD, want)
            self.assertEqual(values(out)[1], values(overlay)[1])

    def test_dlc_budget_floor_is_still_merged_without_shrinking_mods(self):
        for budget in (100, 9999):
            overlay = table({HUD: 40000000, 'Actor/Test.byml': budget, 'Aoc/0010/Actor/Test.byml': 200})
            out = self.run_probe(overlay=overlay, dlc=True).stdout
            self.assert_budget(out, 'Actor/Test.byml', max(budget, 200))

    def test_malformed_and_oversize_overlay_stops_before_publication(self):
        valid = table({HUD: 1, 'Actor/Mod.bfres': 10})
        unordered = valid[:12] + valid[20:28] + valid[12:20]
        unterminated = b'RSTB' + struct.pack('<II', 0, 1) + b'x' * 128 + struct.pack('<I', 1)
        huge = b'Yaz0' + struct.pack('>I', 0xffffffff) + bytes(8)
        for overlay in (b'', b'RSTB', b'RSTB' + struct.pack('<II', 0xffffffff, 0),
                        valid[:-1], unordered, unterminated, huge, literal(valid)[:-2]):
            with self.subTest(overlay=overlay[:16]):
                result = self.run_probe(overlay=overlay, expected=7)
                self.assertEqual(result.stdout, b'')
                self.assertIn(b'Mod ResourceSizeTable is unreadable or invalid', result.stderr)

    def test_growth_overflow_is_rejected(self):
        self.run_probe(mode='arithmetic')
        result = self.run_probe(original=table({HUD: 0xfffffff0}), expected=7)
        self.assertIn(b'budget overflow', result.stderr)

    def test_invalid_original_table_stops_before_publication(self):
        result = self.run_probe(original=b'RSTB' + struct.pack('<II', 0xffffffff, 0), expected=7)
        self.assertEqual(result.stdout, b'')
        self.assertIn(b'ResourceSizeTable is unreadable or invalid', result.stderr)

    def test_long_crc_paths_can_update_and_insert_without_name_field_limit(self):
        path = 'Voice/' + 'x' * 140 + '.bars'
        for original in (table({path: 123, 'a': 7}), table({'a': 7})):
            with self.subTest(existing=zlib.crc32(path.encode()) in values(original)[0]):
                out = self.run_probe(original=original, mode='raise:' + path).stdout
                self.assert_budget(out, path, 32728448)
                self.assert_budget(out, 'a', 7)


if __name__ == '__main__':
    unittest.main()
