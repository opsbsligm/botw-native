"""The Steam Deck keeps the Deck prompt family in every Steam input mode (regression for the 20261106 cutover)."""
from pathlib import Path
import shlex,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class FamilyPolicy(unittest.TestCase):
    @unittest.skipUnless(sys.platform=='linux','Production Linux/SDL family policy')
    def test_deck_name_wins_over_input_mode(self):
        cflags=shlex.split(subprocess.check_output(['pkg-config','--cflags','sdl3'],text=True))
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as d:
            exe=Path(d)/'family-policy'
            subprocess.run(['clang','-std=gnu17','-march=x86-64-v3','-O0','-I',str(ROOT/'runtime'),*cflags,str(ROOT/'tests/deck_family_policy_test.c'),'-o',str(exe)],check=True,timeout=60)
            subprocess.run([str(exe)],check=True,timeout=15)
