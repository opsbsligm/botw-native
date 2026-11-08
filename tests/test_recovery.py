"""Validate archive/part boundaries before any decryption or filesystem install."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('recovery',ROOT/'tools/recovery.py')
recovery=importlib.util.module_from_spec(spec);spec.loader.exec_module(recovery)


class Recovery(unittest.TestCase):
    def test_portable_runtime_entry_remains_executable_after_roundtrip(self):
        # A test-only byte relay isolates archive/mode behavior. This is not a
        # cryptographic authentication test; private integration uses real age.
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as tmp:
            root=Path(tmp);source=root/'input';source.mkdir();bundle=root/'bundle';output=root/'restored'
            entry=source/'run.sh';entry.write_text('#!/bin/sh\nprintf "portable-runtime-ok\\n"\n');entry.chmod(0o755)
            data=source/'settings.ini';data.write_text('fps=60\n');data.chmod(0o600)
            relay=root/'byte-relay';relay.write_text('#!/bin/sh\nif [ "$1" = "-R" ]; then cat > "$4"; else cat; fi\n');relay.chmod(0o700)
            with patch.object(recovery,'age_tool',return_value=str(relay)):
                recovery.seal(SimpleNamespace(destination=str(bundle),include=['runtime='+str(source)],label='synthetic-runtime',missing=[],recipients='unused'))
                recovery.restore(SimpleNamespace(bundle=str(bundle),identity='unused',destination=str(output),max_bytes=1024**2))
            restored=output/'runtime/run.sh'
            self.assertEqual(subprocess.check_output([str(restored)],text=True),'portable-runtime-ok\n')
            self.assertEqual(restored.stat().st_mode & 0o7777,0o700)
            self.assertEqual((output/'runtime/settings.ini').stat().st_mode & 0o7777,0o600)
            self.assertEqual((output/'runtime/settings.ini').read_bytes(),data.read_bytes())
            self.assertEqual(entry.stat().st_mode & 0o7777,0o755)

    def test_archive_names_cannot_escape_or_alias(self):
        for name in ('../keys','/etc/passwd','dump/../../file','dump//file','dump/./file','dump\\file',''):
            with self.subTest(name=name),self.assertRaises(ValueError):recovery.safe_name(name)
        self.assertEqual(str(recovery.safe_name('dump/romfs/assets.bin')),'dump/romfs/assets.bin')

    def test_corrupt_part_cannot_create_restored_tree(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as tmp:
            root=Path(tmp);bundle=root/'bundle';bundle.mkdir();output=root/'restored'
            (bundle/'payload-00000.age.part').write_bytes(b'changed')
            index=dict(schema=1,cipher='age',parts=[dict(name='payload-00000.age.part',bytes=7,sha256='0'*64)])
            (bundle/'index.json').write_text(json.dumps(index))
            with self.assertRaisesRegex(ValueError,'corrupt'):
                recovery.restore(SimpleNamespace(bundle=str(bundle),identity='unused',destination=str(output),max_bytes=1024))
            self.assertFalse(output.exists())

    def test_part_name_cannot_read_outside_bundle(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as tmp:
            root=Path(tmp);index=dict(schema=1,cipher='age',parts=[dict(name='../identity',bytes=1,sha256='0'*64)])
            (root/'index.json').write_text(json.dumps(index))
            with self.assertRaisesRegex(ValueError,'sequence'):
                recovery.restore(SimpleNamespace(bundle=str(root),identity='unused',destination=str(root/'out'),max_bytes=1024))
