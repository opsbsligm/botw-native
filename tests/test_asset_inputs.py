"""Do not certify bytes changed by another process during decoder validation."""
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import pipeline
import legacy_sdk7


class AssetInputs(unittest.TestCase):
    def test_mutating_movie_during_decode_is_not_certified(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            work, dump = root / 'work', root / 'dump'
            for path in (work, dump / 'romfs/System', dump / 'romfs/Movie'):
                path.mkdir(parents=True, exist_ok=True)
            (dump / 'romfs/System/Version.txt').write_text('1.6.0')
            movie = dump / 'romfs/Movie/fixture.webm'
            movie.write_bytes(b'decoder input fixture')
            def concurrent_mutation(*args):
                movie.write_bytes(b'changed by another process')
            with patch.object(legacy_sdk7, 'verify_exefs'), patch.dict(os.environ, NATIVE_DEPS='/not-used'), \
                 patch.object(pipeline, 'run', side_effect=concurrent_mutation):
                with self.assertRaisesRegex(ValueError, 'changed during'):
                    pipeline.assets(work, dump, 1)
            self.assertFalse((work / 'validated-assets/movies.json').exists())


if __name__ == '__main__':
    unittest.main()
