"""Exercise final publication, font notices, and decoded-input binding."""
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import package_linux as package
import legacy_sdk7


class PackageTests(unittest.TestCase):
    def test_publish_with_font_notice_and_validated_movie(self):
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)
            root, work, dump = (base / name for name in ('source', 'work', 'dump'))
            for path in (root / 'third_party/fonts/a', root / 'third_party/luts', root / 'packaging',
                         root / 'licenses', work / 'game', work / 'validated-assets',
                         dump / 'exefs', dump / 'romfs/Movie', dump / 'romfs/System'):
                path.mkdir(parents=True, exist_ok=True)
            for name in ('LICENSE', 'NOTICE.md'):
                (root / name).write_text('notice')
            (root / 'UPSTREAM.json').write_text(json.dumps({'game_version': '1.6.0'}))
            (root / 'third_party/fonts/a/OFL.txt').write_text('retain this notice')
            for name in ('run.py', 'run.sh', 'game.sh', 'motion_session.py'):
                (root / 'packaging' / name).write_text('launcher')
            for name in ('savagewind', 'swgame.so', 'swgame.so.id'):
                (work / 'game' / name).write_bytes(b'compiled test fixture')
            (dump / 'romfs/System/Version.txt').write_text('1.6.0')
            movie = dump / 'romfs/Movie/fixture.webm'
            movie.write_bytes(b'decoder-validated fixture')
            records = {'romfs/Movie/fixture.webm': hashlib.sha256(movie.read_bytes()).hexdigest()}
            (work / 'validated-assets/movies.json').write_text(json.dumps(records))
            original_copy = shutil.copy2

            def copy(source, dest, *args, **kwargs):
                if str(source).startswith('/usr/share/'):
                    Path(dest).write_bytes(b'public font fixture')
                    return str(dest)
                return original_copy(source, dest, *args, **kwargs)

            with patch.object(package, 'ROOT', root), patch.object(package, 'ensure_clean'), \
                 patch.object(package, 'revision', return_value='a' * 40), patch.object(package, 'verify'), \
                 patch.object(legacy_sdk7, 'verify_exefs'), patch.object(package, 'bundle_libraries'), \
                 patch.object(package, 'archive_sources', side_effect=lambda r, p: p.write_bytes(b'public source')), \
                 patch.object(package.shutil, 'copy2', side_effect=copy), \
                 patch.object(sys, 'argv', ['package', '--work', str(work), '--dump', str(dump)]):
                package.main()
            output = work / 'package'
            self.assertTrue((output / 'bundle.json').is_file())
            self.assertEqual((output / 'licenses/fonts/a/OFL.txt').read_text(), 'retain this notice')
            self.assertFalse((work / 'package.new').exists())
            movie.write_bytes(b'changed after decoding')
            with self.assertRaisesRegex(ValueError, 'Movie changed'):
                package.verify_movies(work, dump)
            movie.unlink()
            with self.assertRaisesRegex(ValueError, 'inventory changed'):
                package.verify_movies(work, dump)


if __name__ == '__main__':
    unittest.main()
