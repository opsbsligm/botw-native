"""Exercise production world/help semantic isolation using an own layout fixture."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HelpLabels(unittest.TestCase):
    def test_world_buttons_footer_and_malformed_layouts(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as directory:
            exe = Path(directory) / 'world-help'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'runtime'),
                str(ROOT / 'tests/deck_help_labels_test.c'), '-lm', '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=15)
