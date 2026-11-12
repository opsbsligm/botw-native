"""Completion requires successful real engine accounting and a readable outcome record."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == 'linux', 'Production Linux precompile completion')
class PrecompileCompletion(unittest.TestCase):
    def test_production_engine_outcomes_and_record_validation(self):
        flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'sdl3', 'vulkan'], text=True))
        deps = Path(os.environ.get('NATIVE_DEPS', '/opt/native-deps/build/linux/x86_64/deps'))
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as folder:
            binary = Path(folder) / 'precompile-completion'
            subprocess.run([shutil.which('clang') or 'cc', '-std=gnu11', '-O1',
                '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                '-I', str(ROOT / 'runtime'), '-isystem', str(deps / 'include'), *flags,
                str(ROOT / 'tests/precompile_completion_test.c'), '-o', str(binary), '-lm', '-lpthread'],
                check=True, timeout=60)
            subprocess.run([str(binary)], cwd=folder, check=True, timeout=30)
