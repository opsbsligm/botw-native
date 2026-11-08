/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE 1
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../runtime/kernel.c"
#include "../runtime/ipc.h"
#undef SERVICE
#define SERVICE(name,fn) static void fn(Service *s,IpcReq *req,IpcResp *resp)
#include "../runtime/services/misc.c"

_Noreturn void rt_fatal_trace(void) {abort();}
const char *plat_symbol(uint64_t address,uint64_t *offset) {(void)address;(void)offset;return NULL;}
static IpcResp command(Service *service,unsigned number) {
    uint64_t handle=1;IpcReq request={.cmd=number,.data=(const uint8_t *)&handle,.data_size=8};IpcResp response={0};
    nfp_user(service,&request,&response);assert(!response.result);return response;
}
static NfpSession *session(Service *service) {
    NfpSession *state=calloc(1,sizeof *state);assert(state);state->next=nfp_sessions;nfp_sessions=state;service->state=state;
    command(service,17);command(service,18);return state;
}
static int signaled(NfpSession *state,int index) {KObject *event=handle_get(state->event[index]);assert(event && event->type==KOBJ_EVENT);return event->signaled;}
static uint32_t model(Service *service,uint64_t address,uint64_t size) {
    uint64_t handle=1;IpcReq request={.cmd=16,.data=(const uint8_t *)&handle,.data_size=8,.nb=1};
    request.b[0]=(IpcBuf){address,size};IpcResp response={0};
    nfp_user(service,&request,&response);return response.result;
}
int main(void) {
    Service first={0},second={0};NfpSession *a=session(&first),*b=session(&second);
    command(&first,0);command(&first,3);assert(!nd_amiibo_scan_virtual(0));
    assert(signaled(a,0) && !signaled(a,1));assert(signaled(b,0) && !signaled(b,1));
    unsigned char previous[0x58]={0},tag[0x58];assert(!nd_amiibo_tag_info(previous,sizeof previous));command(&first,5);
    for(int i=0;i<2048;i++) {
        assert(!nd_amiibo_scan_virtual(0));
        assert(!signaled(a,0) && signaled(a,1));assert(!signaled(b,0) && signaled(b,1));
        command(&first,3);assert(signaled(a,0) && !signaled(a,1));assert(signaled(b,0) && !signaled(b,1));
        assert(!nd_amiibo_tag_info(tag,sizeof tag));assert(memcmp(previous,tag,7));memcpy(previous,tag,sizeof tag);command(&first,5);
    }
    /* Returning into a host temporary buffer is not delivery to the guest.
     * Bounds failures and rejected/partial process_vm_writev must not acknowledge
     * the current identity. Use real protected Linux mappings, not a copy mock. */
    NdAmiiboSnapshot status;nd_amiibo_snapshot(&status);assert(!status.current_identity_read && !status.model_reads);
    assert(model(&first,GUEST_LO-1,0x40)==ND_NFP_NOT_FOUND);
    nd_amiibo_snapshot(&status);assert(!status.current_identity_read && !status.model_reads);
    long page=sysconf(_SC_PAGESIZE);assert(page>=4096);
    void *output=mmap((void *)(uintptr_t)(GUEST_LO+0x1000000),(size_t)page*2,PROT_READ|PROT_WRITE,
                     MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(output==(void *)(uintptr_t)(GUEST_LO+0x1000000));memset(output,0xa5,(size_t)page*2);
    assert(model(&first,(uintptr_t)output,0x3f)==ND_NFP_NOT_FOUND);
    assert(!mprotect(output,(size_t)page,PROT_READ));
    assert(model(&first,(uintptr_t)output,0x40)==ND_NFP_NOT_FOUND);
    nd_amiibo_snapshot(&status);assert(!status.current_identity_read && !status.model_reads);
    assert(!mprotect(output,(size_t)page,PROT_READ|PROT_WRITE));
    assert(!mprotect((char *)output+page,(size_t)page,PROT_NONE));
    assert(model(&first,(uintptr_t)output+(size_t)page-32,0x40)==ND_NFP_NOT_FOUND);
    nd_amiibo_snapshot(&status);assert(!status.current_identity_read && !status.model_reads);
    assert(!model(&first,(uintptr_t)output,0x40));
    assert(!memcmp(output,"\x01\x01\x03\x09\x14\x04\x00",7));
    nd_amiibo_snapshot(&status);assert(status.current_identity_read && status.model_reads==1);
    assert(!mprotect(output,(size_t)page,PROT_READ));
    assert(model(&first,(uintptr_t)output,0x40)==ND_NFP_NOT_FOUND);
    nd_amiibo_snapshot(&status);assert(status.current_identity_read && status.model_reads==1);
    assert(!munmap(output,(size_t)page*2));
    puts("ModelInfo: only complete guest delivery acknowledges the current identity PASS");
    command(&first,4);assert(!signaled(a,0) && signaled(a,1));
    KObject *closed_activate=handle_get(a->event[0]);assert(closed_activate);nfp_close(&first);
    uint32_t w,r;KObject *unrelated=kernel_create_event_pair(&w,&r);assert(!unrelated->signaled);
    command(&second,3);assert(signaled(b,0) && !signaled(b,1));assert(!closed_activate->signaled && !unrelated->signaled);
    nd_amiibo_remove();assert(!signaled(b,0) && signaled(b,1));assert(!unrelated->signaled);
    nfp_close(&second);assert(!nfp_sessions);assert(!nd_amiibo_scan_virtual(0));assert(!unrelated->signaled);
    puts("NFP IPC: 2048 actual event transitions, UID rotation and session-close isolation PASS");return 0;
}
