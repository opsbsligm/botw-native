"""Assemble a private, portable directory from verified local build outputs."""
import argparse
from build_receipts import verify
from source_tree import ensure_clean, revision, archive_sources
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BASE_LIBS = re.compile(r'^(?:ld-linux|lib(?:c|m|dl|pthread|rt|resolv|stdc\+\+|gcc_s|vulkan)\.so)')


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def copy_tree(source, dest):
    if not source.is_dir():
        raise ValueError('Missing input directory: ' + str(source))
    for path in source.rglob('*'):
        if path.is_symlink():
            raise ValueError('Private package inputs must not contain symlinks: ' + str(path))
    shutil.copytree(source, dest)


def bundle_libraries(binary, directory):
    directory.mkdir(exist_ok=True)
    pending = [binary]
    seen = set()
    while pending:
        path = pending.pop()
        text = subprocess.check_output(['ldd', str(path)], text=True)
        if 'not found' in text:
            raise ValueError('Unresolved library: ' + text)
        for line in text.splitlines():
            match = re.match(r'\s*(\S+) => (/\S+) \(', line)
            if not match:
                continue
            name, location = match.groups()
            if BASE_LIBS.match(name) or name in seen:
                continue
            seen.add(name)
            target = directory / name
            shutil.copy2(Path(location).resolve(strict=True), target)
            subprocess.run(['patchelf', '--set-rpath', '$ORIGIN', str(target)], check=True)
            pending.append(Path(location))
    subprocess.run(['patchelf', '--set-rpath', '$ORIGIN/lib', str(binary)], check=True)



def verify_movies(work, data):
    """Bind the packaged movie bytes to the completed decoder validation."""
    records = json.loads((work / 'validated-assets/movies.json').read_text())
    movies = {path.relative_to(data).as_posix(): path for path in (data / 'romfs/Movie').glob('*.webm')}
    if not records or set(movies) != set(records):
        raise ValueError('Movie inventory changed after asset validation')
    for name, path in movies.items():
        if path.is_symlink() or digest(path) != records[name]:
            raise ValueError('Movie changed after asset validation: ' + name)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--dump', type=Path, required=True)
    p.add_argument('--code-mods', type=Path)
    a = p.parse_args()
    work, dump = a.work.resolve(), a.dump.resolve()
    ensure_clean(ROOT)
    commit = revision(ROOT)
    verify(work, 'code')
    verify(work, 'assets')
    profile = json.loads((ROOT / 'UPSTREAM.json').read_text())
    target = work / 'package'
    staging = work / 'package.new'
    if target.exists() or staging.exists():
        p.error('Package already exists; move it aside explicitly before creating a replacement')
    staging.mkdir()
    try:
        from legacy_sdk7 import verify_exefs
        verify_exefs(dump / 'exefs', ['main', 'subsdk0', 'sdk'])
        from legacy_mods import RECORD, verify_context
        prepared, static_mods = None, None
        if a.code_mods is not None:
            prepared, static_mods = verify_context(work / 'generated', dump / 'exefs', a.code_mods)
        elif (work / 'generated' / RECORD).exists():
            raise ValueError('Generated code contains static mods; package with the same --code-mods inputs')
        verify_movies(work, dump)
        executable = 'savagewind'
        shutil.copy2(work / 'game' / executable, staging / executable)
        (staging / 'data').mkdir()
        for name in ('exefs', 'romfs'):
            copy_tree(dump / name, staging / 'data' / name)
        if prepared is not None:
            for name, raw in prepared.modules.items():
                # 与生成器相同的快照；运行时 rodata/data 也必须使用修改后的 NSO。
                path = staging / 'data/exefs' / name
                path.unlink()
                path.write_bytes(raw)
            provenance = staging / 'provenance'
            provenance.mkdir(exist_ok=True)
            (provenance / RECORD).write_text(json.dumps(static_mods, sort_keys=True, indent=2) + '\n')
        verify_movies(work, staging / 'data')
        for name in ('aoc1', 'aoc2'):
            if (dump / name).is_dir():
                copy_tree(dump / name, staging / 'data' / name)
        if (staging / 'data/romfs/System/Version.txt').read_text().strip() != '1.6.0':
            raise ValueError('Expected BOTW 1.6.0 RomFS')
        for suffix in ('so', 'so.id'):
            shutil.copy2(work / 'game' / ('swgame.' + suffix), staging / 'data' / ('swgame.' + suffix))
        for font in (ROOT / 'third_party/fonts').rglob('*'):
            if font.is_file() and font.suffix in ('.ttf', '.otf'):
                shutil.copy2(font, staging / font.name)
            elif font.is_file() and font.suffix == '.txt':
                notice = staging / 'licenses/fonts' / font.relative_to(ROOT / 'third_party/fonts')
                notice.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(font, notice)
        shutil.copy2('/usr/share/fonts/truetype/wqy/wqy-microhei.ttc', staging / 'wqy-microhei.ttc')
        shutil.copy2('/usr/share/doc/fonts-wqy-microhei/copyright', staging / 'WenQuanYi-LICENSE.txt')
        copy_tree(ROOT / 'third_party/luts', staging / 'colour-luts')
        bundle_libraries(staging / executable, staging / 'lib')
        shutil.copytree(ROOT / 'licenses', staging / 'licenses', dirs_exist_ok=True)
        for package in ('libssl3t64', 'libvpx9', 'libopus0', 'libzstd1', 'zlib1g'):
            copyright = Path('/usr/share/doc') / package / 'copyright'
            if copyright.is_file():
                shutil.copy2(copyright, staging / 'licenses' / (package + '-copyright.txt'))
        for rel in ('run.py', 'run.sh', 'game.sh', 'motion_session.py'):
            shutil.copy2(ROOT / 'packaging' / rel, staging / rel)
        for rel in ('run.sh', 'game.sh', executable):
            (staging / rel).chmod(0o755)
        for name in ('LICENSE', 'NOTICE.md', 'UPSTREAM.json'):
            shutil.copy2(ROOT / name, staging / name)
        archive_sources(ROOT, staging / 'source.tar.gz')
        manifest = dict(schema=1, source_commit=commit, version=profile['game_version'],
            private_game_content=True, hardware_validated=False,
            files={path.relative_to(staging).as_posix(): dict(size=path.stat().st_size, sha256=digest(path))
                   for path in sorted(staging.rglob('*')) if path.is_file()})
        if static_mods is not None:
            manifest['static_mods'] = static_mods
        (staging / 'bundle.json').write_text(json.dumps(manifest, indent=2) + '\n')
        staging.rename(target)
        print('Private package ready: ' + str(target))
    except BaseException:
        # Keep a failed staging directory for diagnosis. Never publish it as package/.
        raise


if __name__ == '__main__':
    main()
