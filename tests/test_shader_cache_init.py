"""Concurrent first writes wait for the real directories; failed persistence stays a failure."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ShaderCacheInit(unittest.TestCase):
    def test_concurrent_first_writes_and_blocked_cache_path(self):
        compiler = shutil.which('clang')
        self.assertIsNotNone(compiler, 'The fixed Linux SDK must provide clang')
        deps = Path(os.environ.get('NATIVE_DEPS', '/opt/native/deps'))
        with tempfile.TemporaryDirectory(prefix='shader-cache-init-') as temporary:
            binary = Path(temporary) / 'cache-init'
            command = [compiler, '-std=gnu11', '-O1', '-ffunction-sections', '-fdata-sections',
                '-Wl,--gc-sections', '-I' + str(ROOT / 'runtime'),
                '-I' + str(ROOT / 'third_party/vma'), '-I' + str(deps / 'include'),
                str(ROOT / 'tests/shader_cache_init_test.c'), '-lz', '-lpthread', '-o', str(binary)]
            built = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            for mode, message in [('parallel', '16 concurrent first-use production cache writes persisted'),
                                  ('blocked', 'Blocked production cache write rejected')]:
                with self.subTest(mode=mode):
                    checked = subprocess.run([str(binary), str(Path(temporary) / mode), mode],
                        capture_output=True, text=True, timeout=10)
                    self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)
                    self.assertIn(message, checked.stdout)


if __name__ == '__main__':
    unittest.main()
