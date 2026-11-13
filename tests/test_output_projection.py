# SPDX-License-Identifier: MIT
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class OutputProjection(unittest.TestCase):
    @unittest.skipUnless(sys.platform == 'linux', 'Production Linux projection runtime')
    def test_actual_projection_math_and_unavailable_feature_fallbacks(self):
        with tempfile.TemporaryDirectory(prefix='projection-runtime-', dir=Path.cwd()) as directory:
            exe = Path(directory) / 'projection'
            subprocess.run(['clang', '-std=gnu17', '-O1', '-ffunction-sections', '-fdata-sections',
                '-Wl,--gc-sections', str(ROOT / 'tests/output_projection_test.c'), '-o', str(exe),
                '-lm', '-lpthread'], check=True, timeout=45)
            subprocess.run([str(exe)], check=True, timeout=5)
