"""The production SDK7 clock preserves elapsed game time and 30Hz hold counts."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LegacyClock(unittest.TestCase):
    def test_game_time_and_hold_ticks_are_independent_of_present_rate(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as tmp:
            output = Path(tmp) / 'clock.so'
            subprocess.run(['cc', '-std=c11', '-shared', '-fPIC',
                str(ROOT / 'runtime/legacy_clock.c'), '-o', str(output)], check=True)
            clock = ctypes.CDLL(str(output))
            clock.sdk7_frame_presented.argtypes = [ctypes.c_uint64]
            clock.sdk7_frame_ratio.restype = ctypes.c_float
            clock.sdk7_pad_tick.restype = ctypes.c_uint
            now = 1_000_000_000
            clock.sdk7_frame_presented(now)
            for hz in (30, 60, 90, 120, 60, 30):
                step = round(1e9 / hz)
                for _ in range(10):
                    now += step
                    clock.sdk7_frame_presented(now)
                seconds, ticks = 0, 0
                for _ in range(hz * 3):
                    now += step
                    clock.sdk7_frame_presented(now)
                    seconds += clock.sdk7_frame_ratio() / 30
                    ticks += clock.sdk7_pad_tick()
                self.assertAlmostEqual(seconds, 3.0, places=5)
                self.assertLessEqual(abs(ticks - 90), 1)
            previous = clock.sdk7_frame_ratio()
            clock.sdk7_frame_presented(now + 5_000_000_000)
            self.assertEqual(clock.sdk7_frame_ratio(), previous)
