"""Fault-inject pthread creation failures into the actual Vulkan warm-up pool launcher."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PrecompileWorkers(unittest.TestCase):
    def test_creation_failure_does_not_drop_work_or_join_invalid_threads(self):
        source = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text()
        start = source.index('static int run_precompile_workers(')
        end = source.index('\n}\n', start) + 3
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as directory:
            case = Path(directory) / 'precompile-workers.c'
            case.write_text(
                (ROOT / 'tests/precompile_workers_test.c').read_text()
                + '\n#define pthread_create test_create\n#define pthread_join test_join\n'
                + source[start:end]
            )
            executable = Path(directory) / 'precompile-workers'
            subprocess.run(
                ['clang', '-std=gnu11', '-O1', '-Wall', '-Wextra', '-Werror',
                 '-pthread', str(case), '-o', str(executable)], check=True, timeout=60,
            )
            subprocess.run([str(executable)], check=True, timeout=30)
