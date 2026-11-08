"""Deck's touch profile must survive an existing enabled mouse-camera preference."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('touch_profile_launcher', Path(__file__).resolve().parents[1] / 'packaging/run.py')
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)


class TouchProfile(unittest.TestCase):
    def test_deck_models_unknown_hardware_and_explicit_overrides(self):
        with tempfile.TemporaryDirectory() as directory:
            hardware = Path(directory)
            self.assertEqual(launcher.touchscreen_priority({}, hardware), '0')
            (hardware / 'sys_vendor').write_text('Valve\n')
            for model in ('Jupiter', 'Galileo'):
                (hardware / 'product_name').write_text(model + '\n')
                self.assertEqual(launcher.touchscreen_priority({}, hardware), '1')
                self.assertEqual(launcher.touchscreen_priority({'SAVAGEWIND_TOUCH_PRIORITY': '0'}, hardware), '0')
            (hardware / 'sys_vendor').write_text('Other\n')
            self.assertEqual(launcher.touchscreen_priority({}, hardware), '0')
            (hardware / 'sys_vendor').write_text('Valve\n')
            (hardware / 'product_name').write_text('Unknown\n')
            self.assertEqual(launcher.touchscreen_priority({}, hardware), '0')
            self.assertEqual(launcher.touchscreen_priority({'SAVAGEWIND_TOUCH_PRIORITY': '1'}, hardware), '1')
            with self.assertRaisesRegex(ValueError, 'must be 0 or 1'):
                launcher.touchscreen_priority({'SAVAGEWIND_TOUCH_PRIORITY': 'auto'}, hardware)
