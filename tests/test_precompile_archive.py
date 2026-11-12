"""Asset-free fixture for the real graphics pipeline library prewarm entry point."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from make_synthetic_shader_archive import create


@unittest.skipUnless(sys.platform == 'linux', 'Linux Vulkan compiler toolchain')
class SyntheticPrecompileArchive(unittest.TestCase):
    def test_cli_exit_status(self):
        deps = Path(os.environ.get('NATIVE_DEPS', '/opt/native-deps/build/linux/x86_64/deps'))
        flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'sdl3', 'vulkan'], text=True))
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as tmp:
            binary = Path(tmp) / 'cli-status'
            subprocess.run(['clang', '-std=gnu11', '-O1',
                '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                '-I', str(ROOT / 'runtime'), '-isystem', str(deps / 'include'), *flags,
                str(ROOT / 'tests/precompile_cli_exit_test.c'), str(ROOT / 'runtime/platform/linux.c'),
                '-lm', '-lpthread', '-o', str(binary)], check=True, timeout=60)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_production_warm_stage_layouts(self):
        deps = Path(os.environ.get('NATIVE_DEPS', '/opt/native-deps/build/linux/x86_64/deps'))
        flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'sdl3', 'vulkan'], text=True))
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as tmp:
            binary = Path(tmp) / 'warm-stage'
            subprocess.run(['clang', '-std=gnu11', '-O1', '-ffunction-sections', '-fdata-sections',
                '-Wl,--gc-sections', '-I', str(ROOT / 'runtime'), '-isystem', str(deps / 'include'), *flags,
                str(ROOT / 'tests/precompile_stage_layout_test.c'), '-lm', '-lpthread', '-o', str(binary)],
                check=True, timeout=90)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_actual_archive_reader_and_compiler(self):
        deps = Path(os.environ.get('NATIVE_DEPS', '/opt/native-deps/build/linux/x86_64/deps'))
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as tmp:
            root = Path(tmp)
            fixture = create(root / 'fixture')
            with self.assertRaises(ValueError):
                create(fixture)
            sources = ['tests/precompile_archive_test.c', 'runtime/nvn/shader_archive.c',
                'runtime/nvn/maxwell/translate.c', 'runtime/romfs_image.c', 'runtime/romfs_build.c']
            binary = root / 'archive-check'
            subprocess.run(['clang', '-std=gnu11', '-O1', '-I', str(ROOT / 'runtime'),
                '-isystem', str(deps / 'include'), *[str(ROOT / p) for p in sources],
                '-L', str(deps / 'lib'), '-Wl,-rpath,' + str(deps / 'lib'), '-lshaderc_shared',
                '-lm', '-lpthread', '-o', str(binary)], check=True, timeout=60)
            subprocess.run([str(binary), str(fixture / 'romfs')], check=True, timeout=15)
