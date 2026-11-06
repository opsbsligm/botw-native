"""Keep Deck help artwork after keyboard input and respect explicit overrides."""
from pathlib import Path
import shlex,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class DiagramSelection(unittest.TestCase):
    @unittest.skipUnless(sys.platform=='linux','Production Linux/SDL glyph selection')
    def test_hardware_keyboard_overrides_and_hotplug_generation(self):
        cflags=shlex.split(subprocess.check_output(['pkg-config','--cflags','sdl3'],text=True))
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as d:
            exe=Path(d)/'diagram-selection'
            subprocess.run(['cc','-std=gnu17','-O1','-ffunction-sections','-fdata-sections','-I',str(ROOT/'runtime'),*cflags,str(ROOT/'tests/deck_diagram_selection.c'),'-Wl,--gc-sections','-lm','-o',str(exe)],check=True,timeout=60)
            subprocess.run([str(exe)],check=True,timeout=15)
