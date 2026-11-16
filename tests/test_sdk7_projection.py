# SPDX-License-Identifier: MIT
"""Original miniature NSOs exercise guarded production AOT and register effects."""
import contextlib
import hashlib
import io
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import gen
import legacy_clock
import legacy_mods
import legacy_sdk7 as profile
from code_mods import Patch, apply_patches, decode_nso
from nso import Nso
from test_layout import module, RET


class ProjectionAot(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='sdk7-projection-', dir=Path.cwd())
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.exefs = self.root / 'exefs'
        self.exefs.mkdir()
        self.sites = {name: (0x40 + i * 8, word)
                      for i, (name, (_, word)) in enumerate(profile.PROJECTION_SITES.items())}
        pins = {}
        guards = []
        camera_guards = []
        for index, name in enumerate(('main', 'subsdk0', 'sdk')):
            raw = bytearray(decode_nso(module(name, 0x1000, 0x1000, 0x1000,
                                             9 if name == 'main' else 1, [], 0x50 + index)[0]).data)
            if name == 'main':
                raw[0x40:0x60] = bytes.fromhex(profile.BUILD_ID).ljust(32, b'\0')
                for offset, word in self.sites.values():
                    body = struct.pack('<II', word, RET)
                    raw[0x100 + offset:0x108 + offset] = body
                    guards.append((offset, offset + 8, hashlib.sha256(body).hexdigest()))
                for camera_index, offset in enumerate((0x200, 0x210, 0x220)):
                    body = struct.pack('<II', 0xd2800000 | ((camera_index + 1) << 5), RET)
                    raw[0x100 + offset:0x108 + offset] = body
                    camera_guards.append((offset, offset + 8, hashlib.sha256(body).hexdigest()))
                for segment in range(3):
                    file_at, _, size = struct.unpack_from('<III', raw, 0x10 + segment * 0x10)
                    raw[0xa0 + segment * 32:0xc0 + segment * 32] = hashlib.sha256(raw[file_at:file_at + size]).digest()
            (self.exefs / name).write_bytes(raw)
            pins[name] = hashlib.sha256(raw).hexdigest()
        for patch in [mock.patch.dict(profile.PINS, pins, clear=True),
                      mock.patch.object(profile, 'PROJECTION_SITES', self.sites),
                      mock.patch.object(profile, 'PROJECTION_FUNCTIONS', tuple(guards)),
                      mock.patch.object(profile, 'CAMERA_EVENT_GUARDS', tuple(camera_guards)),
                      mock.patch.object(profile, 'CAMERA_EVENT_OFFSET', 0x2500),
                      mock.patch.object(legacy_clock, 'GUARDS', {})]:
            patch.start()
            self.addCleanup(patch.stop)

    def module(self):
        nso = Nso(self.exefs / 'main')
        return SimpleNamespace(nso=nso, base=profile.MAIN_BASE,
                               sites={**profile.projection_sites(), **profile.camera_event_sites(nso)})

    def test_camera_context_and_metadata_changes_cannot_generate_hooks(self):
        m = self.module()
        m.nso.image[0x204] ^= 1
        with self.assertRaisesRegex(ValueError, 'camera event: context hash'):
            profile.projection_table(m)
        m = self.module()
        m.sites.pop('sdk7_camera_event_mgr')
        with self.assertRaisesRegex(ValueError, 'camera event: metadata'):
            profile.projection_table(m)
        with mock.patch.object(profile, 'CAMERA_EVENT_OFFSET', 0x2501), \
                self.assertRaisesRegex(ValueError, 'singleton location'):
            profile.camera_event_sites(m.nso)

    def test_changed_method_or_missing_site_cannot_generate_hooks(self):
        m = self.module()
        offset = next(iter(self.sites.values()))[0]
        m.nso.image[offset + 4] ^= 1  # Unreplaced instruction in the same method.
        with self.assertRaisesRegex(ValueError, 'method hash'):
            profile.projection_table(m)
        m = self.module()
        m.sites.pop('persp_matrix')
        with self.assertRaisesRegex(ValueError, 'site mismatch'):
            profile.projection_table(m)
        m = self.module()
        m.nso.build_id = 'ff'
        with self.assertRaisesRegex(ValueError, 'identity'):
            profile.projection_table(m)

    def test_static_mods_cannot_change_replaced_method_context(self):
        raw = (self.exefs / 'main').read_bytes()
        ranges = legacy_mods.reserved_ranges(raw, 'main', set())
        for offset, _ in self.sites.values():
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, '保留'):
                apply_patches(decode_nso(raw), [Patch(0x100 + offset + 4, b'\0')], ranges)
        for offset in (0x200, 0x210, 0x220):
            with self.subTest(camera_context=offset), self.assertRaisesRegex(ValueError, '保留'):
                apply_patches(decode_nso(raw), [Patch(0x100 + offset + 4, b'\0')], ranges)

    @unittest.skipUnless(sys.platform == 'linux', 'Production Linux toolchain')
    def test_all_nine_hooks_compile_and_update_only_the_expected_registers(self):
        generated = self.root / 'generated'
        argv = ['gen.py', 'all', str(self.exefs), str(generated), '--legacy-sdk7', '--simd', 'c']
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(legacy_clock, 'table', return_value={}), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            gen.main()
        layout = (generated / 'layout.c').read_text()
        for name in self.sites:
            self.assertIn('"' + name + '"', layout)
        self.assertIn('"sdk7_camera_event_mgr"', layout)
        # Stubs deliberately change each input differently. Swapped registers,
        # lost upper vector bits, or an untouched original load will fail.
        harness = '''#include "cpu.h"
#include <assert.h>
#include <string.h>
volatile int g_fnprof;
void fnprof_hit(uint64_t a,uint64_t b,uint64_t d) {(void)a;(void)b;(void)d;}
void rt_persp(uint64_t p,uint32_t *t,uint32_t *a) {(void)p;*t+=16;*a+=32;}
void rt_persp_offset(uint64_t p,uint32_t *x,uint32_t *y) {(void)p;*x+=64;*y+=128;}
void rt_frustum(uint64_t p,float *f) {(void)p;f[0]=11;f[1]=12;f[2]=13;f[3]=14;}
'''
        pairs = {'persp_matrix': [(2, 0x3f800010), (3, 0x40000020)],
                 'persp_matrix_offset': [(4, 0x40400040), (5, 0x40800080)],
                 'persp_unproject_y': [(1, 0x3f800010)],
                 'persp_unproject_x': [(2, 0x3f800010), (3, 0x40000020)],
                 'frustum_lr0': [(0, 0x41300000), (1, 0x41400000)],
                 'frustum_lr1': [(2, 0x41300000), (3, 0x41400000)],
                 'frustum_tb0': [(0, 0x41500000), (2, 0x41600000)],
                 'frustum_tb1': [(2, 0x41500000), (3, 0x41600000)]}
        checks = []
        for name, (offset, _) in self.sites.items():
            fn = f'f_{profile.MAIN_BASE + offset:x}'
            harness += f'void {fn}(Cpu *);\n'
            case = f'c=initial; expected=initial; {fn}(&c);\n'
            if name == 'proj_update':
                case += 'expected.x[8]=1;\n'
            for register, value in pairs.get(name, []):
                case += f'expected.v[{register}]={value}u;\n'
            case += 'assert(!memcmp(&c,&expected,sizeof c));\n'
            checks.append(case)
        harness += '''int main(void) {
 unsigned char projection[256]={0}; uint32_t values[]={0x3f800000,0x40000000,0x40400000,0x40800000};
 memcpy(projection+0xac,values,sizeof values);
 Cpu initial={0},c,expected;
 for(int i=0;i<32;i++) initial.v[i]=~(__uint128_t)0;
 for(int i=0;i<31;i++) initial.x[i]=0x12340000+i;
 initial.x[0]=(uint64_t)(uintptr_t)projection;
''' + ''.join(checks) + 'return 0; }\n'
        source = self.root / 'execute.c'
        source.write_text(harness)
        clang = shutil.which('clang')
        self.assertIsNotNone(clang)
        executable = self.root / 'execute'
        chunks = sorted((generated / 'main').glob('main_[0-9]*.c'))
        result = subprocess.run([clang, '-std=gnu11', '-O1', '-ffunction-sections', '-fdata-sections',
            '-I' + str(ROOT / 'runtime'), '-Wl,--gc-sections', *map(str, chunks), str(source), '-lm', '-o', str(executable)],
            capture_output=True, text=True, timeout=45)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(subprocess.run([executable], timeout=5).returncode, 0)
