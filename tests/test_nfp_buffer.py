"""Bad IPC descriptors must fail without faulting or writing host memory."""
from pathlib import Path
import subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
@unittest.skipUnless(sys.platform=='linux','Linux kernel-copy backend')
class NfpBuffers(unittest.TestCase):
    def test_real_guard_pages_readonly_host_and_overflow(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as temp:
            p=Path(temp);source=p/'check.c';binary=p/'check'
            source.write_text(r'''
#include "nfp_buffer.h"
#include <assert.h>
#include <string.h>
#include <sys/mman.h>
int main(void) {
 const uint64_t lo=0x7000000000ull,hi=0x7320000000ull;
 unsigned char data[0x100];memset(data,0x5a,sizeof data);
 unsigned char host[0x100];memset(host,0xcc,sizeof host);
 assert(!nd_nfp_write(0,data,8,lo,hi));assert(!nd_nfp_write(1,data,8,lo,hi));
 assert(!nd_nfp_write((uintptr_t)host,data,8,lo,hi));assert(host[0]==0xcc);
 assert(!nd_nfp_write(UINT64_MAX-3,data,8,lo,hi));assert(!nd_nfp_write(hi-4,data,8,lo,hi));
 void *mapping=mmap((void *)(lo+0x10000000),8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);assert(mapping!=MAP_FAILED);
 uint64_t address=(uintptr_t)mapping;assert(!nd_nfp_write(address,NULL,8,lo,hi));assert(!nd_nfp_write(address,data,0,lo,hi));
 for(unsigned n=8;n<=0x58;n+=8) {assert(nd_nfp_write(address,data,n,lo,hi));assert(!memcmp(mapping,data,n));}
 assert(!mprotect(mapping,4096,PROT_READ));assert(!nd_nfp_write(address,data,8,lo,hi));
 assert(!mprotect(mapping,4096,PROT_READ|PROT_WRITE));assert(!mprotect((char *)mapping+4096,4096,PROT_NONE));
 assert(!nd_nfp_write(address+4092,data,0x58,lo,hi));assert(!munmap(mapping,8192));assert(!nd_nfp_write(address,data,8,lo,hi));return 0;
}
''')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'runtime'),str(source),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=10)
