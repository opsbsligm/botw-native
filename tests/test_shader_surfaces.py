"""Compile original surface instructions and verify the production SPIR-V resource contract."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SurfaceShaders(unittest.TestCase):
    def test_original_surface_instructions_compile_and_reflect_as_storage_images(self):
        compiler = shutil.which('clang')
        self.assertIsNotNone(compiler, 'The fixed Linux SDK must provide clang')
        deps = Path(os.environ.get('NATIVE_DEPS', '/opt/native/deps'))
        self.assertTrue((deps / 'include/shaderc/shaderc.h').is_file(), 'The fixed SDK shaderc headers are required')
        with tempfile.TemporaryDirectory(prefix='surface-shaders-') as temporary:
            target = Path(temporary) / 'surface-shaders'
            command = [compiler, '-std=gnu11', '-O1', '-ffunction-sections', '-fdata-sections',
                       '-Wl,--gc-sections', '-fuse-ld=lld', '-I' + str(ROOT / 'runtime'),
                       '-I' + str(ROOT / 'third_party/vma'), '-I' + str(deps / 'include'),
                       str(ROOT / 'tools/test_surface_shaders.c'),
                       str(ROOT / 'runtime/nvn/maxwell/translate.c'),
                       str(ROOT / 'runtime/nvn/vulkan/shaders.c'), '-L' + str(deps / 'lib'),
                       '-lshaderc_shared', '-lstdc++', '-lm', '-o', str(target)]
            built = subprocess.run(command, capture_output=True, text=True, timeout=90)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            env = os.environ.copy()
            env['LD_LIBRARY_PATH'] = str(deps / 'lib') + ':' + env.get('LD_LIBRARY_PATH', '')
            result = subprocess.run([str(target)], capture_output=True, text=True, env=env, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('4 SPIR-V/resource cases and 10 rejection cases passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
