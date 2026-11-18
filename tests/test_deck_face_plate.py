"""Deck face prompts keep the physical button's rounded square, at the game's own footprint."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile, unittest
ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / 'third_party/fonts/FiraSans/FiraSans-SemiBold.ttf'
class DeckFacePlate(unittest.TestCase):
    @unittest.skipUnless(sys.platform == 'linux', 'Production Linux/SDL glyph geometry')
    def test_plate_is_square_and_footprint_unchanged(self):
        cflags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'sdl3'], text=True))
        libs = shlex.split(subprocess.check_output(['pkg-config', '--libs', 'sdl3'], text=True))
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as d:
            exe = Path(d) / 'deck-face-plate'
            # glyphs.c pulls in the SIMD helpers, which only build on the production
            # toolchain (clang with the vector builtins the x86-64-v3 path enables).
            subprocess.run(['clang', '-std=gnu17', '-march=x86-64-v3', '-O1', '-I', str(ROOT / 'runtime'),
                            *cflags, str(ROOT / 'tests/deck_face_plate_test.c'), *libs, '-lm', '-o', str(exe)],
                           check=True, timeout=120)
            subprocess.run([str(exe)], check=True, timeout=15)
            # The letter test measures the capital actually knocked out of the plate, so it needs the
            # font the runtime loads. Naming it here keeps the check independent of the job's cwd.
            env = {**os.environ, 'NATIVE_FONT': str(FONT)}
            subprocess.run([str(exe), 'letter'], check=True, timeout=15, env=env)
