"""Exercise actual NFC service and kernel event handles across repeated figure scans."""
from pathlib import Path
import subprocess,sys,tempfile,unittest

ROOT=Path(__file__).resolve().parents[1]

@unittest.skipUnless(sys.platform=='linux','Linux host runtime')
class NfpEventLevels(unittest.TestCase):
    def test_rescan_and_session_close_preserve_exclusive_event_levels(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as temp:
            executable=Path(temp)/'check'
            subprocess.run(['clang','-std=gnu17','-O1','-ffunction-sections','-fdata-sections',
                '-pthread',str(ROOT/'tests/nfp_event_levels_test.c'),
                str(ROOT/'runtime/native_amiibo.c'),'-Wl,--gc-sections','-o',str(executable)],check=True)
            subprocess.run([str(executable)],check=True,timeout=15)
