# SPDX-License-Identifier: MIT
"""Compile actual loader and event readers with original in-memory guest data."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CameraEvent(unittest.TestCase):
    @unittest.skipUnless(sys.platform == 'linux', 'Production Linux runtime')
    def test_guarded_slot_optional_loading_event_lifecycle_and_pacing_isolation(self):
        source = (ROOT / 'runtime/hle/framepace.c').read_text()
        begin = source.index('static bool event_at(')
        end = source.index('// The game\'s UI screen', begin)
        with tempfile.TemporaryDirectory(prefix='sdk7-camera-event-', dir=Path.cwd()) as temporary:
            directory = Path(temporary)
            case = directory / 'camera-event.c'
            case.write_text((ROOT / 'tests/sdk7_camera_event_test.c').read_text() + '\n' + source[begin:end])
            binary = directory / 'camera-event'
            subprocess.run(['clang', '-D_GNU_SOURCE', '-std=gnu17', '-O1',
                '-ffunction-sections', '-fdata-sections', '-I', str(ROOT / 'runtime'),
                str(case), '-Wl,--gc-sections', '-o', str(binary), '-ldl'],
                check=True, timeout=45)
            subprocess.run([str(binary)], check=True, timeout=5)
