// Small services: parental control, add-on content, and other mostly-stubbed interfaces.
#include "../nfp_buffer.h"
#include <string.h>
#include <stdlib.h>
#include "../native_amiibo.h"

#include "../hle.h"
#include "../ipc.h"

static void ok_logger(Service *s, IpcReq *req, IpcResp *resp) {
    LOG("[svc] %s cmd %u (ok)\n", s->name, req->cmd);
    resp_size(resp, 0x10);
}

static uint32_t new_event_handle(void) {
    uint32_t w, r;
    kernel_create_event_pair(&w, &r);
    return r;
}

// ------------------------------------------------------------------ pctl
static void pctl_service(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 1031: case 1035: case 1018: OUT(uint8_t, 0) = 0; resp_size(resp, 1); break;  // restrictions off
    case 1032: OUT(uint32_t, 0) = 0; resp_size(resp, 4); break;
    default: LOG("[pctl] IParentalControlService cmd %u (ok)\n", req->cmd); resp_size(resp, 0x10);
    }
}

static void pctl_root(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    if (req->cmd == 0 || req->cmd == 1)
        ipc_return_object(resp, service_new("IParentalControlService", pctl_service, NULL));
}
SERVICE("pctl", pctl) { pctl_root(s, req, resp); }
SERVICE("pctl:s", pctl_s) { pctl_root(s, req, resp); }
SERVICE("pctl:a", pctl_a) { pctl_root(s, req, resp); }
SERVICE("pctl:r", pctl_r) { pctl_root(s, req, resp); }

// ------------------------------------------------------------------ aoc:u
// The add-on contents set up (services/fs.c: an image per index) are the ones installed; their data is mounted
// through fsp-srv OpenDataStorageByDataId.
static int aoc_list(uint32_t *out, int max) {
    int n = 0;
    for (int i = 1; i <= 8; i++)
        if (fs_aoc_present(i)) { if (out && n < max) out[n] = (uint32_t)i; n++; }
    return n;
}

SERVICE("aoc:u", aoc_u) {
    (void)s;
    switch (req->cmd) {
    case 0: case 2: OUT(uint32_t, 0) = (uint32_t)aoc_list(NULL, 0); resp_size(resp, 4); break;  // CountAddOnContent
    case 1: case 3: {  // ListAddOnContent(offset, count) -> B buffer of indices
        uint32_t idx[8], offset = IN(uint32_t, 0), count = IN(uint32_t, 4);
        int n = aoc_list(idx, 8), w = 0;
        IpcBuf *b = out_buf(req, 0);
        uint32_t room = b ? (uint32_t)(b->size / 4) : 0;
        if (count > room) count = room;
        for (uint32_t i = offset; i < (uint32_t)n && (uint32_t)w < count; i++)
            ((uint32_t *)(uintptr_t)b->addr)[w++] = idx[i];
        LOG("[aoc] ListAddOnContent(%u, %u): %d of %d\n", offset, count, w, n);
        OUT(uint32_t, 0) = (uint32_t)w;
        resp_size(resp, 4);
        break;
    }
    case 4: case 5: OUT(uint64_t, 0) = fs_aoc_base_id(); resp_size(resp, 8); break;  // GetAddOnContentBaseId
    case 6: case 7: LOG("[aoc] PrepareAddOnContent %u\n", IN(uint32_t, 0)); break;
    case 8: case 10: resp->copy_handles[resp->ncopy++] = new_event_handle(); break;  // ...ListChangedEvent
    default: ok_logger(s, req, resp);
    }
}

// ------------------------------------------------------------------ nfp:user: read-only virtual figure reader
// Each session owns the writable event handles. Never signal a recycled guest handle.
typedef struct NfpSession {uint32_t event[3];struct NfpSession *next;} NfpSession;
static NfpSession *nfp_sessions;
static pthread_mutex_t nfp_lock=PTHREAD_MUTEX_INITIALIZER;
static void nfp_notify(int activated) {
    pthread_mutex_lock(&nfp_lock);klock();
    for(NfpSession *s=nfp_sessions;s;s=s->next) {
        KObject *event=handle_get(s->event[activated?0:1]);
        if(event && event->type==KOBJ_EVENT) kernel_signal(event);
    }
    pthread_mutex_unlock(&g_klock);pthread_mutex_unlock(&nfp_lock);
}
static void nfp_close(Service *service) {
    NfpSession *s=service->state;pthread_mutex_lock(&nfp_lock);
    NfpSession **link=&nfp_sessions;while(*link && *link!=s) link=&(*link)->next;
    if(*link) *link=s->next;
    for(int i=0;i<3;i++) if(s->event[i]) kernel_close_handle(s->event[i]);
    pthread_mutex_unlock(&nfp_lock);free(s);
}
static void nfp_user(Service *service,IpcReq *req,IpcResp *resp) {
    NfpSession *session=service->state;NdAmiiboSnapshot state;nd_amiibo_snapshot(&state);
    if(req->cmd!=0 && req->cmd!=1 && req->cmd!=2 && req->cmd!=19 && req->cmd!=23) {
        if(req->data_size<8 || IN(uint64_t,0)!=1) {resp->result=ND_NFP_NOT_FOUND;return;}
    }
    switch(req->cmd) {
    case 0: nd_amiibo_set_notify(nfp_notify);nd_amiibo_initialize(1);break;
    case 1: nd_amiibo_stop();nd_amiibo_initialize(0);break;
    case 2: {
        IpcBuf *b=out_buf(req,0);int count=state.initialized && b && b->size>=8;
        if(count) {uint64_t id=1;if(!nd_nfp_write(b->addr,&id,8,GUEST_LO,GUEST_HI)) {resp->result=ND_NFP_NOT_FOUND;break;}}
        OUT(int32_t,0)=count;resp_size(resp,4);break;
    }
    case 3: resp->result=nd_amiibo_start();break;
    case 4: resp->result=nd_amiibo_stop();break;
    case 5: resp->result=nd_amiibo_mount();break;
    case 6: resp->result=nd_amiibo_unmount();break;
    case 13: case 16: {
        IpcBuf *b=out_buf(req,0);size_t need=req->cmd==13?0x58:0x40;
        if(!b || b->size<need) {resp->result=ND_NFP_NOT_FOUND;break;}
        unsigned char metadata[0x58];
        resp->result=req->cmd==13?nd_amiibo_tag_info(metadata,sizeof metadata):nd_amiibo_model_info(metadata,sizeof metadata);
        if(!resp->result && !nd_nfp_write(b->addr,metadata,need,GUEST_LO,GUEST_HI)) resp->result=ND_NFP_NOT_FOUND;
        break;
    }
    case 17: case 18: case 23: {
        int index=req->cmd==17?0:req->cmd==18?1:2;uint32_t w,r;
        kernel_create_event_pair(&w,&r);
        pthread_mutex_lock(&nfp_lock);
        if(session->event[index]) kernel_close_handle(session->event[index]);
        session->event[index]=w;pthread_mutex_unlock(&nfp_lock);
        if(index==2 || (index==0 && (state.state==2 || state.state==4))) {
            klock();kernel_signal(handle_get(w));pthread_mutex_unlock(&g_klock);
        }
        resp->copy_handles[resp->ncopy++]=r;break;
    }
    case 19: OUT(uint32_t,0)=state.initialized;resp_size(resp,4);break;
    case 20: OUT(uint32_t,0)=state.state;resp_size(resp,4);break;
    case 21: OUT(uint32_t,0)=0;resp_size(resp,4);break;
    default: resp->result=ND_NFP_NOT_FOUND;break; // unsupported application-area writes never pretend to succeed
    }
}
SERVICE("nfp:user",nfp_user_manager) {
    (void)s;
    if(req->cmd!=0) {resp->result=ND_NFP_NOT_FOUND;return;}
    NfpSession *state=calloc(1,sizeof *state);
    if(!state) {resp->result=ND_NFP_NOT_FOUND;return;}
    Service *user=service_new("IUser(nfp)",nfp_user,state);user->on_close=nfp_close;
    pthread_mutex_lock(&nfp_lock);state->next=nfp_sessions;nfp_sessions=state;pthread_mutex_unlock(&nfp_lock);
    ipc_return_object(resp,user);
}
