"""Default budgets and strict SDK7 opt-in, including the non-Linux C branch."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GuestMemoryProfile(unittest.TestCase):
    def exercise(self, flags):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as temporary:
            executable = Path(temporary) / 'memory-profile'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', *flags,
                '-I', str(ROOT / 'runtime'), str(ROOT / 'tests/guest_memory_profile_test.c'),
                '-o', str(executable)], check=True)
            result = subprocess.run([str(executable)], check=True, capture_output=True, text=True)
            self.assertEqual(result.stdout, 'guest application memory profile passed\n')

    def test_native_platform_defaults_profile_and_strict_opt_in(self):
        self.exercise([])

    def test_non_linux_profile_keeps_original_application_memory(self):
        self.exercise(['-U__linux__'])
