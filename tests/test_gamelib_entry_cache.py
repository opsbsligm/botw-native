"""Compile the production ABI entry across a zero-mtime generator change."""
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import build_receipts
import compiler_cache
import pipeline
import source_tree

ABI_HEADERS = ['runtime/cpu.h','runtime/simd.h','runtime/simd_fp.h',
               'runtime/simd_softfma.h','runtime/legacy_clock.h','runtime/gamelib.h','Makefile']


@unittest.skipUnless(sys.platform == 'linux', 'Production Linux make entry')
class GameLibraryEntryCache(unittest.TestCase):
    def test_generator_change_refreshes_actual_entry_id_without_rebuilding_guest_objects(self):
        self.assertTrue(os.environ.get('NATIVE_DEPS'), 'Use the pinned native CI toolchain')
        names = source_tree.source_files(ROOT)
        with tempfile.TemporaryDirectory(prefix='gamelib-entry-',dir=Path.cwd()) as directory:
            case=Path(directory);source=case/'source';source.mkdir()
            records={}
            # Use only declared public source, supporting both a checkout and
            # its Git-free archive. All output remains outside this fixture.
            for name in names:
                original=ROOT/name;self.assertFalse(original.is_symlink())
                target=source/name;target.parent.mkdir(parents=True,exist_ok=True)
                raw=original.read_bytes();target.write_bytes(raw)
                target.chmod(stat.S_IMODE(original.stat().st_mode));os.utime(target,(0,0))
                records[name]={'size':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
            def seal_fixture():
                (source/source_tree.MANIFEST).write_text(json.dumps({
                    'schema':1,'commit':'0'*40,'files':records})+'\n')
            seal_fixture()
            work=case/'work';work.mkdir();out=work/'game'
            entry=out/'obj/tools/gamelib_entry.o'
            printer=case/'print-id.mk'
            printer.write_text(".PHONY: print-cache-test-id\nprint-cache-test-id:\n\t@printf '%s\\n' '$(GAMELIB_ID)'\n")
            def library_id():
                value=subprocess.check_output(['make','-s','-f','Makefile','-f',str(printer),
                    'print-cache-test-id','B='+str(out),'GEN='+str(work/'generated'),
                    'DEPS='+os.environ['NATIVE_DEPS']],cwd=source,text=True,timeout=30).strip()
                self.assertRegex(value,r'^[0-9a-f]{16}$');return value
            harness=case/'entry-check.c'
            harness.write_text(r'''
#include "gamelib.h"
#include <string.h>
const GameLibModule game_modules[1]={{0}};
const uint32_t game_module_count=0;
const char game_version[]="original-test-fixture";
const GameLibSite game_sites[1]={{0}};
const uint32_t game_site_count=0;
const GameLibTables *swgame_tables(void);
int main(int argc,char **argv) {
    if(argc!=2) return 1;
    const GameLibTables *tables=swgame_tables();
    if(tables->abi!=GAMELIB_ABI) return 2;
    return strcmp(tables->id,argv[1])?3:0;
}
''')
            def check_actual_entry(expected):
                binary=case/'entry-check'
                subprocess.run(['clang','-std=gnu17','-I'+str(source/'runtime'),str(harness),
                    str(entry),'-lm','-o',str(binary)],check=True,timeout=30)
                subprocess.run([str(binary),expected],check=True,timeout=5)
            real_run=pipeline.run
            def run_in_fixture(work,label,args,**kwargs):
                return real_run(work,label,args,cwd=source,**kwargs)
            with mock.patch.object(pipeline,'ROOT',source), \
                 mock.patch.object(build_receipts,'ROOT',source), \
                 mock.patch.object(pipeline,'run',side_effect=run_in_fixture):
                before_abi=compiler_cache.guest_abi(source,ABI_HEADERS)
                before_id=library_id()
                pipeline.make(work,'entry-before',1,str(entry))
                check_actual_entry(before_id)
                before_entry_hash=hashlib.sha256(entry.read_bytes()).hexdigest()
                sentinel=out/'obj/main/cache-sentinel.o';sentinel.parent.mkdir(parents=True)
                subprocess.run(['clang','-x','c','-c','-','-o',str(sentinel)],
                    input=b'int original_guest_fixture(void){return 7;}\n',check=True,timeout=20)
                sentinel_state=(hashlib.sha256(sentinel.read_bytes()).hexdigest(),sentinel.stat().st_mtime_ns)
                generator=source/'tools/legacy_sdk7.py'
                self.assertEqual(generator.stat().st_mtime_ns,0)
                generator.write_bytes(generator.read_bytes()+b'\n# Original ABI cache regression fixture.\n')
                os.utime(generator,(0,0))
                records['tools/legacy_sdk7.py']={'size':generator.stat().st_size,
                    'sha256':hashlib.sha256(generator.read_bytes()).hexdigest()}
                seal_fixture()
                self.assertEqual(compiler_cache.guest_abi(source,ABI_HEADERS),before_abi)
                after_id=library_id();self.assertNotEqual(after_id,before_id)
                pipeline.make(work,'entry-after',1,str(entry))
                check_actual_entry(after_id)
                self.assertNotEqual(hashlib.sha256(entry.read_bytes()).hexdigest(),before_entry_hash)
                self.assertEqual((hashlib.sha256(sentinel.read_bytes()).hexdigest(),sentinel.stat().st_mtime_ns),sentinel_state)
                cache=json.loads((out/'compiler-inputs.json').read_text())
                self.assertEqual(cache,{'host':build_receipts.source_stamp(),'guest':before_abi})
