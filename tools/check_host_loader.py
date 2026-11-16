"""Exercise the compiled production host with original, non-game ELF fixtures.

Run in the pinned Linux native-build job. All fixtures are temporary under the
job workspace; only the small result joins the existing seven-day log artifact.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

FIXTURE = r'''
#include "gamelib.h"
#include <stdlib.h>
#include <sys/prctl.h>
/* Check the actual post-exec host state before any fixture code can fail. */
__attribute__((constructor)) static void require_nondumpable(void) {
    if (prctl(PR_GET_DUMPABLE, 0L, 0L, 0L, 0L) != 0) _Exit(9);
}
extern volatile int g_fnprof;
/* The shared object needs a real host export. Calling this guest function is
 * always a failure: metadata inspection must never dispatch guest code. */
static void poison_guest(Cpu *cpu) {(void)cpu;g_fnprof=1;__builtin_trap();}
static const uint64_t count=1, addresses[]={0x800000000ull};
static const GuestFn functions[]={poison_guest};
const GameLibModule game_modules[]={
 {"main",0x800000000ull,0x3000000,
  "8e9978d50bdd20b4c8395a106c27ffde00000000000000000000000000000000",
  addresses,functions,&count},
 {"subsdk0",0x804000000ull,0x1000,"original-public-fixture",addresses,functions,&count},
 {"sdk",0x805000000ull,0x1000,"original-public-fixture",addresses,functions,&count}
};
const uint32_t game_module_count=3;
const char game_version[]="1.6.0";
const GameLibSite game_sites[]={{"sdk7_camera_event_mgr",0x802cc56d8ull}};
const uint32_t game_site_count=1;
'''


def limits():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_DATA, (512 * 1024**2, 512 * 1024**2))
    resource.setrlimit(resource.RLIMIT_CPU, (15, 15))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    runtime = args.runtime.resolve(strict=True)
    work = args.work.resolve(strict=True)
    results = []
    with tempfile.TemporaryDirectory(prefix='host-loader-', dir=work) as directory:
        case = Path(directory)
        printer = case/'print-id.mk'
        printer.write_text(".PHONY: loader-fixture-id\nloader-fixture-id:\n\t@printf '%s\\n' '$(GAMELIB_ID)'\n")
        identity = subprocess.check_output(['make', '-s', '--no-print-directory',
            '-f', 'Makefile', '-f', str(printer), 'loader-fixture-id',
            'B='+str(work/'game'), 'DEPS='+os.environ['NATIVE_DEPS']],
            cwd=ROOT, text=True, timeout=30).strip()
        if len(identity) != 16 or any(c not in '0123456789abcdef' for c in identity):
            raise ValueError('Invalid production Make identity')
        source = case/'fixture.c'
        source.write_text(FIXTURE)
        def compile_library(name, *, embedded=None, extra=None):
            library = case/(name+'.so')
            inputs = [str(source), str(ROOT/'tools/gamelib_entry.c')]
            if extra is not None:
                custom = case/(name+'.c');custom.write_text(extra);inputs = [str(custom)]
            subprocess.run(['clang', '-std=gnu17', '-shared', '-fPIC', '-O2',
                '-I'+str(ROOT/'runtime'), '-DGAMELIB_ID="'+(embedded or identity)+'"',
                *inputs, '-o', str(library)], check=True, timeout=30)
            library.with_suffix('.so.id').write_text(identity+'\n')
            return library
        def inventory():
            return {p.relative_to(case).as_posix(): (p.stat().st_size,
                hashlib.sha256(p.read_bytes()).hexdigest())
                for p in case.rglob('*') if p.is_file()}
        def check(name, library, status, expected_error=None):
            before = inventory()
            env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                SAVAGEWIND_DATA_ROOT=str(case/'must-not-create-data'))
            result = subprocess.run([str(runtime), '--verify-code', str(library)],
                cwd=case, env=env, capture_output=True, text=True, timeout=20,
                preexec_fn=limits)
            if result.returncode != status:
                raise AssertionError((name, result.returncode, result.stderr[-1200:]))
            if before != inventory() or (case/'must-not-create-data').exists():
                raise AssertionError('Inspection changed files or created a data root')
            if status == 0:
                value = json.loads(result.stdout)
                if value != {'schema':1, 'kind':'game-code-metadata', 'abi':3,
                             'id':identity, 'modules':3, 'sites':1}:
                    raise AssertionError((name, value))
            elif expected_error and expected_error not in result.stderr:
                raise AssertionError((name, 'Unexpected rejection reason'))
            results.append({'case':name, 'exit':result.returncode,
                            'files_unchanged':True, 'guest_not_called':True})
        good = compile_library('correct')
        check('actual-host-exports-and-metadata', good, 0)
        wrong = compile_library('stale-embedded-id', embedded='0000000000000000')
        check('matching-sidecar-stale-embedded-id', wrong, 2, 'another version')
        good.with_suffix('.so.id').write_text('0000000000000000\n')
        check('mismatched-sidecar', good, 2, 'sidecar')
        good.with_suffix('.so.id').unlink()
        check('missing-sidecar', good, 2, 'sidecar')
        good.with_suffix('.so.id').write_text(identity+'\n')
        abi = compile_library('wrong-abi', extra=r'''
#include "gamelib.h"
GAMELIB_EXPORT const GameLibTables *swgame_tables(void) {
    static const GameLibTables t={.abi=4,.id=GAMELIB_ID};return &t;
}
''')
        check('wrong-abi', abi, 2, 'another version')
        missing = compile_library('missing-entry', extra='int public_fixture=1;\n')
        check('missing-entry', missing, 2, 'not game code')
        unresolved = compile_library('missing-host-export', extra=r'''
#include "gamelib.h"
extern volatile int original_fixture_missing_host_export;
GAMELIB_EXPORT const GameLibTables *swgame_tables(void) {
    static const GameLibTables t={.abi=3,.id=GAMELIB_ID};
    return original_fixture_missing_host_export ? 0 : &t;
}
''')
        check('missing-host-export', unresolved, 2, 'undefined symbol')
        missing.unlink()
        check('missing-library', missing, 2)
        check('correct-after-negative-cases', good, 0)
    report = {'schema':1, 'cases':results, 'actual_production_host':True,
              'actual_production_entry_getter':True, 'private_game_inputs':False,
              'native_dumpability_checked_by_fixture':True,
              'gameplay_validated':False, 'temporary_fixtures_removed':True}
    (work/'logs').mkdir(exist_ok=True)
    (work/'logs/host-loader-verification.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
