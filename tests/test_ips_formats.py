"""Standard IPS records must really write zeros and fail without partial changes."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == 'linux', 'Production Linux mod integration and sanitizers')
class IpsFormats(unittest.TestCase):
    def test_standard_formats_and_runtime_dispatch(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as tmp:
            binary = Path(tmp) / 'ips-formats'
            subprocess.run(['cc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                '-I', str(ROOT / 'runtime'), str(ROOT / 'tests/ips_formats_test.c'),
                '-pthread', '-o', str(binary)], check=True, timeout=60)
            subprocess.run([str(binary)], cwd=tmp, check=True, timeout=15)
