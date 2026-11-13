"""Malformed on-disk metadata must become a cache miss before runtime consumes its arrays."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ShaderCacheBounds(unittest.TestCase):
    def test_real_cache_writer_and_loader_reject_corrupted_counts(self):
        compiler = shutil.which('clang')
        self.assertIsNotNone(compiler, 'The fixed Linux SDK must provide clang')
        deps = Path(os.environ.get('NATIVE_DEPS', '/opt/native/deps'))
        with tempfile.TemporaryDirectory(prefix='shader-cache-bounds-') as temporary:
            binary = Path(temporary) / 'cache-bounds'
            command = [compiler, '-std=gnu11', '-O1', '-ffunction-sections', '-fdata-sections',
                '-Wl,--gc-sections', '-I' + str(ROOT / 'runtime'),
                '-I' + str(ROOT / 'third_party/vma'), '-I' + str(deps / 'include'),
                str(ROOT / 'tests/shader_cache_bounds_test.c'), '-lz', '-lpthread', '-o', str(binary)]
            built = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            checked = subprocess.run([str(binary), str(Path(temporary) / 'cache')],
                capture_output=True, text=True, timeout=10)
            self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)
            self.assertIn('18 keyed/id cases passed', checked.stdout)


if __name__ == '__main__':
    unittest.main()
