"""Rebuild production code and private assets without existing object files."""
import argparse
import fcntl
from build_receipts import publish, source_stamp
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
GAME = 'botw'


def run(work, label, args, *, env=None, cwd=ROOT):
    logs = work / 'logs'
    logs.mkdir(exist_ok=True)
    path = logs / (label + '.log')
    print(label + ': ' + str(path), flush=True)
    with path.open('w') as stream:
        result = subprocess.run(list(map(str, args)), cwd=cwd,
            stdout=stream, stderr=subprocess.STDOUT, env=env)
    if result.returncode:
        print('\n'.join(path.read_text(errors='replace').splitlines()[-50:]), file=sys.stderr)
        raise SystemExit(f'{label} failed ({result.returncode}); full log: {path}')


def tool(work, label, name, *args, env=None):
    run(work, label, [sys.executable, ROOT / 'tools' / name, *args], env=env)


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def make(work, label, jobs, *args):
    from compiler_cache import prepare,complete,guest_abi
    out=work/'game';out.mkdir(exist_ok=True)
    current=dict(host=source_stamp(),guest=guest_abi(ROOT,[
        'runtime/cpu.h','runtime/simd.h','runtime/simd_fp.h','runtime/simd_softfma.h',
        'runtime/legacy_clock.h','runtime/gamelib.h','Makefile']))
    host=[p for d in ('rt','imgui','ffx') for p in (out/d).rglob('*.o')]
    state=prepare(out,current,host,list((out/'obj').rglob('*.o')))
    run(work, label, ['make', '-j' + str(jobs), 'B=' + str(out),
        'GEN=' + str(work / 'generated'), 'DEPS=' + os.environ['NATIVE_DEPS'], *args])
    complete(state,current)


def runtime(work, jobs):
    make(work, 'runtime', jobs, str(work / 'game/savagewind'))


def code(work, dump, jobs):
    from legacy_sdk7 import verify_exefs
    verify_exefs(dump / 'exefs', ['main', 'subsdk0', 'sdk'])
    tool(work, 'generate', 'gen.py', 'all', dump / 'exefs', work / 'generated',
         '--hle', ROOT / 'runtime/hle/hle_symbols.txt', '--simd', 'c', '--legacy-sdk7')
    make(work, 'game', jobs)


def assets(work, dump, jobs):
    from legacy_sdk7 import verify_exefs
    verify_exefs(dump / 'exefs', ['main', 'subsdk0', 'sdk'])
    if (dump / 'romfs/System/Version.txt').read_text().strip() != '1.6.0':
        raise ValueError('Expected BOTW 1.6.0 updated RomFS')
    movies = sorted((dump / 'romfs/Movie').glob('*.webm'))
    if not movies:
        raise ValueError('Missing game movies')
    (work / 'validated-assets').mkdir(exist_ok=True)
    fingerprints = {str(movie.relative_to(dump)): sha(movie) for movie in movies}
    for movie in movies:
        run(work, 'movie-' + movie.stem, [os.environ['NATIVE_DEPS'] + '/bin/ffmpeg',
            '-v', 'error', '-xerror', '-i', movie, '-f', 'null', '-'])
    after = sorted((dump / 'romfs/Movie').glob('*.webm'))
    if after != movies or any(sha(movie) != fingerprints[str(movie.relative_to(dump))] for movie in movies):
        raise ValueError('Movie input changed during decoder validation')
    (work / 'validated-assets/movies.json').write_text(json.dumps(fingerprints, indent=2))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('command', choices=('runtime', 'build'))
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--jobs', type=int, choices=(1, 2, 3, 4), default=2)
    p.add_argument('--stage', choices=('code', 'assets', 'package', 'all'), default='all')
    a = p.parse_args()
    a.work = a.work.resolve()
    a.work.mkdir(parents=True, exist_ok=True)
    (a.work / 'home').mkdir(exist_ok=True)
    from check_toolchain import main as check_toolchain
    check_toolchain()
    if a.command == 'runtime':
        runtime(a.work, a.jobs)
        return
    lock = (a.work / 'build.lock').open('a')
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    before = source_stamp()
    dump = Path('/input')
    receipt = a.work / 'build-complete.json'
    receipt.unlink(missing_ok=True)
    if a.stage in ('code', 'all'):
        (a.work / 'code-receipt.json').unlink(missing_ok=True)
        code(a.work, dump, a.jobs)
        publish(a.work, 'code', [a.work / 'game', a.work / 'generated'], before)
    if a.stage in ('assets', 'all'):
        (a.work / 'assets-receipt.json').unlink(missing_ok=True)
        assets(a.work, dump, a.jobs)
        publish(a.work, 'assets', [a.work / 'validated-assets'], before)
    if a.stage in ('package', 'all'):
        tool(a.work, 'package', 'package_linux.py', '--work', a.work, '--dump', dump)
        receipt.write_text(json.dumps(dict(schema=1, game=GAME, package='package',
            hardware_validated=False), indent=2) + '\n')
    print(a.command + ' / ' + a.stage + ' completed', flush=True)


if __name__ == '__main__':
    main()
