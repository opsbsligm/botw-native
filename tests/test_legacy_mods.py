# SPDX-License-Identifier: MIT
"""原创三模块经生产准备/生成/原生编译执行；测试 profile 只在进程内注入。"""
import contextlib
import hashlib
import io
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import gen
import legacy_clock
import legacy_mods
import legacy_sdk7
from code_mods import Patch, apply_patches, decode_nso
from nso import Nso, DT_GNU_HASH
from test_layout import module, MOVZ_X0


class LegacyModIntegration(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='native-static-aot-', dir=Path.cwd())
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.exefs, self.mods = self.root / 'exefs', self.root / 'mods'
        self.exefs.mkdir()
        self.patch = self.mods / 'test' / 'exefs' / 'value.pchtxt'
        self.patch.parent.mkdir(parents=True)
        self.originals = {}
        for index, name in enumerate(('main', 'subsdk0', 'sdk')):
            raw = bytearray(module(name, 0x1000, 0x1000, 0x1000, 3, [], 0x41 + index)[0])
            if name == 'main':
                raw[0x40:0x60] = bytes.fromhex(legacy_sdk7.BUILD_ID).ljust(32, b'\0')
            self.originals[name] = bytes(raw)
            (self.exefs / name).write_bytes(raw)
        pins = {name: hashlib.sha256(raw).hexdigest() for name, raw in self.originals.items()}
        pin_patch = mock.patch.dict(legacy_sdk7.PINS, pins, clear=True)
        pin_patch.start()
        self.addCleanup(pin_patch.stop)
        guards = mock.patch.object(legacy_clock, 'GUARDS', {0x48: MOVZ_X0 | 0x20})
        guards.start()
        self.addCleanup(guards.stop)
        self.write_patch(0x140, struct.pack('<I', MOVZ_X0 | (42 << 5)))

    def write_patch(self, offset, content):
        self.patch.write_text('@nsobid-' + legacy_sdk7.BUILD_ID + '\n@enabled\n' + f'{offset:08X} {content.hex()}\n')

    def test_preserves_loader_metadata_hle_and_clock_instructions(self):
        for name, raw in self.originals.items():
            nso = Nso(name, raw=raw)
            protected = legacy_mods.reserved_ranges(raw, name, {f'sw_test_{name}_fn0'})
            offsets = [0, 4, 8, nso.dynamic, nso.strtab, nso.dyn[6], nso.relocs[0][0], 0x40]
            if name == 'main':
                offsets.append(0x48)
            else:
                offsets.append(nso.dyn[DT_GNU_HASH])
            for offset in offsets:
                with self.subTest(module=name, offset=offset), self.assertRaisesRegex(ValueError, '保留'):
                    apply_patches(decode_nso(raw), [Patch(0x100 + offset, b'\xFF')], protected)
            self.assertEqual(apply_patches(decode_nso(raw), [Patch(0x100 + 0x50, b'\xFF')], protected)[0x150], 255)

    def test_production_gen_lifts_patched_instruction_and_compiled_function_returns_42(self):
        generated = self.root / 'generated'
        argv = ['gen.py', 'all', str(self.exefs), str(generated), '--legacy-sdk7', '--code-mods', str(self.mods), '--simd', 'c']
        # 此夹具不含真实游戏时钟函数；仅替换这一无关表，生产快照/版本/生成器保持执行。
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(legacy_clock, 'table', return_value={}), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            gen.main()
        prepared, record = legacy_mods.verify_context(generated, self.exefs, self.mods)
        self.assertEqual(prepared.changed_modules, ('main',))
        self.assertEqual(prepared.modules['main'][0x140:0x144], struct.pack('<I', MOVZ_X0 | (42 << 5)))
        self.assertEqual((self.exefs / 'main').read_bytes(), self.originals['main'])
        self.assertEqual(record['inputs']['main'], legacy_sdk7.PINS['main'])
        clang = shutil.which('clang')
        self.assertIsNotNone(clang, 'production Linux toolchain must provide Clang')
        harness = self.root / 'execute.c'
        harness.write_text('''#include "cpu.h"
volatile int g_fnprof;
void fnprof_hit(uint64_t a,uint64_t b,uint64_t c) {(void)a;(void)b;(void)c;}
void f_7000000040(Cpu *);
int main(void) { Cpu c={0}; f_7000000040(&c); return c.x[0]==42 ? 0 : 1; }
''')
        executable = self.root / 'execute'
        chunks = sorted((generated / 'main').glob('main_[0-9]*.c'))
        self.assertTrue(chunks)
        result = subprocess.run([clang, '-std=gnu11', '-O1', '-ffunction-sections', '-fdata-sections',
            '-I' + str(ROOT / 'runtime'), '-Wl,--gc-sections', *map(str, chunks), str(harness), '-lm', '-o', str(executable)],
            capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(subprocess.run([executable], timeout=5).returncode, 0)

    def test_package_rechecks_patch_inputs_and_published_output_instead_of_trusting_receipt(self):
        context = legacy_mods.prepare(self.exefs, self.mods)
        generated = self.root / 'generated'
        generated.mkdir()
        record = legacy_mods.publish_context(context, generated)
        legacy_mods.publish_context(context, generated)  # 完整回读相同内容后才允许复用。
        self.write_patch(0x140, struct.pack('<I', MOVZ_X0 | (43 << 5)))
        with self.assertRaisesRegex(ValueError, '重新生成'):
            legacy_mods.verify_context(generated, self.exefs, self.mods)
        self.write_patch(0x140, struct.pack('<I', MOVZ_X0 | (42 << 5)))
        output = generated / record['directory'] / 'main'
        output.write_bytes(output.read_bytes()[:-1])
        with self.assertRaises(ValueError):
            legacy_mods.verify_context(generated, self.exefs, self.mods)


if __name__ == '__main__':
    unittest.main()
