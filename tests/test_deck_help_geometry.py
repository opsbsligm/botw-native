"""Exercise control endpoints and rejection behavior using authored fixture panes."""
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class HelpGeometry(unittest.TestCase):
    def test_three_modes_joined_leaders_and_atomic_rejection(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as directory:
            exe=Path(directory)/'help-geometry'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'runtime'),
                str(ROOT/'tests/deck_help_geometry_test.c'),'-lm','-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,timeout=15)
