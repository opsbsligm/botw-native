"""Compile synthetic Maxwell TLDS.2D.MS and inspect the actual SPIR-V operands."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == 'linux', 'Vulkan Linux shaderc toolchain')
class TldsMultisample(unittest.TestCase):
    def test_translation_and_spirv(self):
        deps = Path(os.environ.get('NATIVE_DEPS', '/opt/native-deps/build/linux/x86_64/deps'))
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as tmp:
            binary = Path(tmp) / 'tlds-ms'
            subprocess.run([shutil.which('clang') or 'cc', '-std=gnu11', '-O1',
                '-isystem', str(deps / 'include'), '-L', str(deps / 'lib'),
                '-Wl,-rpath,' + str(deps / 'lib'), str(ROOT / 'tests/tlds_multisample_test.c'),
                str(ROOT / 'runtime/nvn/maxwell/translate.c'), '-lshaderc_shared', '-lm', '-o', str(binary)],
                check=True, timeout=60)
            subprocess.run([str(binary)], check=True, timeout=30)

    def test_production_multisample_fallback(self):
        deps = Path(os.environ.get('NATIVE_DEPS', '/opt/native-deps/build/linux/x86_64/deps'))
        flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'sdl3', 'vulkan'], text=True))
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as tmp:
            binary = Path(tmp) / 'multisample-fallback'
            subprocess.run([shutil.which('clang') or 'cc', '-std=gnu11', '-O1',
                '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                '-I', str(ROOT / 'runtime'), '-isystem', str(deps / 'include'), *flags,
                str(ROOT / 'tests/multisample_fallback_test.c'), '-lm', '-lpthread', '-o', str(binary)],
                check=True, timeout=60)
            subprocess.run([str(binary)], check=True, timeout=10)
