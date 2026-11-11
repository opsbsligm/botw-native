"""Extract the FLAN quick-menu step parser from fs.c and verify it on a built fixture and the real lane."""
from pathlib import Path
import re, subprocess, tempfile, unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT / 'runtime/services/fs.c').read_text()


def extract_function(source):
    m = re.search(r'// TEST\(bflan_step_flvi:begin\)\n(.*?^// TEST\(bflan_step_flvi:end\)\n)', source, re.S | re.M)
    assert m, 'bflan_step_flvi TEST markers not found in fs.c'
    return m.group(1)


class QuickmenuFlvi(unittest.TestCase):
    def test_parser_on_built_and_real_lane(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as td:
            td = Path(td)
            inc = td / 'bflan_step_flvi.inc'
            inc.write_text(extract_function(SRC))
            exe = td / 'quickmenu-flvi'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(td), str(ROOT / 'tests/quickmenu_flvi_test.c'), '-lm', '-o', str(exe)],
                           check=True, cwd=ROOT)
            subprocess.run([str(exe)], check=True, timeout=15, cwd=ROOT)


if __name__ == '__main__':
    unittest.main()
