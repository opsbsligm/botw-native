"""The in-play shader warm-up starts once per boot, only when the record is not complete (regression for CA1)."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class WarmupPolicy(unittest.TestCase):
    @unittest.skipUnless(sys.platform=='linux','Production Linux/SDL warm-up')
    def run_with_env(self,env):
        cflags=shlex.split(subprocess.check_output(['pkg-config','--cflags','sdl3'],text=True))
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as d:
            exe=Path(d)/'warmup-policy'
            subprocess.run(['clang','-std=gnu11','-O0','-I',str(ROOT/'runtime'),*cflags,str(ROOT/'tests/warmup_policy_test.c'),'-o',str(exe),'-lm','-lpthread'],check=True,timeout=60)
            subprocess.run([str(exe)],check=True,timeout=60,env=env)
    def test_default(self):
        self.run_with_env({'PATH':os.environ['PATH'],'HOME':str(ROOT)})
    def test_forced_wait(self):
        self.run_with_env({'PATH':os.environ['PATH'],'HOME':str(ROOT),'SAVAGEWIND_SHADER_WARMUP':'2'})
