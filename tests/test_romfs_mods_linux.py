"""Exercise the production Linux RomFS view with real files and rollback."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#define _DEFAULT_SOURCE 1
#include "mods.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
int __real_fsync(int fd);
int __wrap_fsync(int fd) {
 static int injected;
 struct stat actual,parent;
 if(!injected && getenv("MODS_TEST_FAIL_PARENT_SYNC") && !fstat(fd,&actual) &&
    !fstatat(AT_FDCWD,"native-mods",&parent,AT_SYMLINK_NOFOLLOW) &&
    actual.st_dev==parent.st_dev && actual.st_ino==parent.st_ino) {
  injected=1;errno=EIO;fputs("INJECTED_PARENT_FSYNC\n",stderr);return -1;
 }
 return __real_fsync(fd);
}
const char *rt_data_root(void) { return "."; }
int main(int argc, char **argv) {
 if(argc!=2 || chdir(argv[1])) return 2;
 setenv("SAVAGEWIND_ROMFS_OVERLAY","external-user-overlay",1);
 int n=mods_romfs_apply();
 printf("RESULT %d\nOVERLAY %s\n",n,getenv("SAVAGEWIND_ROMFS_OVERLAY"));
 return n<0 ? 1 : 0;
}
'''


@unittest.skipUnless(sys.platform == 'linux', 'Production Linux directory-FD implementation')
class PortableRomfsMods(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = tempfile.TemporaryDirectory(prefix='romfs-mods-compile-', dir=Path.cwd())
        cls.addClassCleanup(cls.compiler.cleanup)
        directory = Path(cls.compiler.name)
        source = directory / 'main.c'
        source.write_text(HARNESS)
        cls.binary = directory / 'check'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-Wl,--wrap=fsync',
                        '-I', str(ROOT / 'runtime'), str(source), str(ROOT / 'runtime/mods.c'),
                        '-o', str(cls.binary)], check=True, capture_output=True, text=True, timeout=30)

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='romfs-mods-data-', dir=Path.cwd())
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.view = self.root / 'native-mods/romfs-view'

    def put(self, name, data=b'mod'):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def apply(self, count=None, fail_sync=False):
        env = dict(os.environ)
        if fail_sync:
            env['MODS_TEST_FAIL_PARENT_SYNC'] = '1'
        result = subprocess.run([str(self.binary), str(self.root)], capture_output=True, text=True, timeout=10, env=env)
        if fail_sync:
            self.assertIn('INJECTED_PARENT_FSYNC', result.stderr)
        if count is None:
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn('OVERLAY external-user-overlay', result.stdout)
        else:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('RESULT ' + str(count) + '\n', result.stdout)
        self.assertEqual(list((self.root / 'native-mods').glob('.romfs-new-*')), [])
        return result

    def test_portable_root_community_layout_disable_and_original_unchanged(self):
        original = self.put('package/data/romfs/System/marker.txt', b'game')
        mod = self.put('mods/10-first/romfs/System/marker.txt', b'replacement')
        self.assertFalse((self.root / 'extracted').exists())
        self.apply(1)
        self.assertEqual((self.view / 'System/marker.txt').read_bytes(), b'replacement')
        self.assertEqual(mod.stat().st_ino, (self.view / 'System/marker.txt').stat().st_ino)
        self.assertEqual(original.read_bytes(), b'game')
        (self.root / 'mods/10-first').rename(self.root / 'mods/.disabled')
        self.apply(0)
        self.assertFalse(self.view.exists())
        self.assertEqual(original.read_bytes(), b'game')

    def test_sorted_override_legacy_layout_and_code_artifacts_are_excluded(self):
        self.put('mods/20-second/romfs/Actor/item.bin', b'last')
        self.put('mods/10-first/romfs/Actor/item.bin', b'first')
        self.put('mods/15-legacy/Map/item.bin', b'legacy')
        self.put('mods/30-code-only/exefs/change.pchtxt')
        self.put('mods/30-code-only/main.ips')
        self.put('mods/30-code-only/README.md')
        self.apply(3)
        self.assertEqual((self.view / 'Actor/item.bin').read_bytes(), b'last')
        self.assertEqual((self.view / 'Map/item.bin').read_bytes(), b'legacy')
        self.assertFalse((self.view / 'exefs').exists())
        self.assertFalse((self.view / 'main.ips').exists())
        self.assertFalse((self.view / 'romfs').exists())

    def test_late_symlink_and_fifo_failure_keep_the_previous_complete_view(self):
        self.put('mods/10-first/romfs/Actor/item.bin', b'healthy')
        self.apply(1)
        (self.root / 'mods/10-first/romfs/Actor/item.bin').unlink()
        self.put('mods/10-first/romfs/Actor/item.bin', b'changed')
        outside = self.put('outside/keep.bin', b'untouched')
        bad = self.root / 'mods/99-bad/romfs/escape'
        bad.parent.mkdir(parents=True)
        bad.symlink_to(outside.parent, target_is_directory=True)
        self.apply()
        self.assertEqual(outside.read_bytes(), b'untouched')
        self.assertEqual((self.view / 'Actor/item.bin').read_bytes(), b'healthy')
        bad.unlink()
        os.mkfifo(bad)
        self.apply()
        self.assertTrue((self.view / 'Actor/item.bin').is_file())
        self.assertFalse((self.view / 'escape').exists())

    def test_unknown_view_or_cache_symlink_is_never_overwritten(self):
        self.put('mods/active/romfs/a.bin')
        sentinel = self.put('native-mods/romfs-view/keep.bin', b'unknown')
        self.apply()
        self.assertEqual(sentinel.read_bytes(), b'unknown')
        sentinel.unlink()
        self.view.rmdir()
        (self.root / 'native-mods').rmdir()
        outside = self.root / 'outside'
        outside.mkdir()
        (self.root / 'native-mods').symlink_to(outside, target_is_directory=True)
        self.apply()
        self.assertEqual(list(outside.iterdir()), [])

    def test_previous_view_cleanup_does_not_follow_an_inserted_symlink(self):
        self.put('mods/active/romfs/a.bin')
        self.apply(1)
        outside = self.put('outside/keep.bin', b'preserve')
        (self.view / 'external').symlink_to(outside.parent, target_is_directory=True)
        self.put('mods/active/romfs/b.bin')
        self.apply(2)
        self.assertEqual(outside.read_bytes(), b'preserve')
        self.assertFalse((self.view / 'external').exists())

    def test_publication_sync_failure_restores_previous_view(self):
        source = self.put('mods/active/romfs/a.bin', b'healthy')
        self.apply(1)
        source.unlink()
        self.put('mods/active/romfs/a.bin', b'new')
        self.apply(fail_sync=True)
        self.assertEqual((self.view / 'a.bin').read_bytes(), b'healthy')

    def test_first_publication_sync_failure_leaves_no_view(self):
        self.put('mods/active/romfs/a.bin')
        self.apply(fail_sync=True)
        self.assertFalse(self.view.exists())

    def test_directory_depth_and_mod_count_fail_closed(self):
        self.put('mods/deep/romfs/' + 'd/' * 34 + 'file.bin')
        self.apply()
        import shutil
        shutil.rmtree(self.root / 'mods')
        for index in range(65):
            self.put(f'mods/{index:02d}/romfs/a.bin')
        self.apply()

    def test_only_marked_staging_from_a_dead_process_is_collected(self):
        self.put('mods/active/romfs/a.bin')
        (self.root / 'native-mods').mkdir(mode=0o700)
        stale = self.root / 'native-mods/.romfs-new-2147483646-1'
        stale.mkdir(mode=0o700)
        (stale / '.native-romfs-view').write_bytes(b'native-romfs-view-v1\n')
        outside = self.put('outside/keep.bin', b'preserve')
        (stale / 'external').symlink_to(outside.parent, target_is_directory=True)
        # Unknown staging contents have no ownership marker and must remain.
        unknown = self.root / 'native-mods/.romfs-new-2147483645-1'
        unknown.mkdir(mode=0o700)
        (unknown / 'keep').write_bytes(b'unknown')
        result = subprocess.run([str(self.binary), str(self.root)], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(stale.exists())
        self.assertEqual((unknown / 'keep').read_bytes(), b'unknown')
        self.assertEqual(outside.read_bytes(), b'preserve')
