"""Deck face prompts keep the physical button's rounded square, at the game's own footprint."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class DeckFacePlate(unittest.TestCase):
    def test_plate_is_square_and_footprint_unchanged(self):
        if not shutil.which('pkg-config') or subprocess.run(['pkg-config','--exists','sdl3']).returncode:
            self.skipTest('SDL3 development files required')
        cflags=subprocess.check_output(['pkg-config','--cflags','sdl3'],text=True).split()
        libs=subprocess.check_output(['pkg-config','--libs','sdl3'],text=True).split()
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as d:
            exe=Path(d)/'deck-face-plate'
            subprocess.run(['cc','-std=gnu17','-O1','-I',str(ROOT/'runtime'),*cflags,
                            str(ROOT/'tests/deck_face_plate_test.c'),*libs,'-lm','-o',str(exe)],
                           check=True,timeout=120)
            subprocess.run([str(exe)],check=True,timeout=15)
