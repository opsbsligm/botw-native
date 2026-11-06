/* SPDX-License-Identifier: MIT */
/* NFP IPC descriptors are guest addresses, never trusted native pointers. */
#pragma once
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <stddef.h>
#include <stdint.h>
#ifdef __linux__
#include <sys/uio.h>
#include <unistd.h>
#endif
static inline int nd_nfp_write(uint64_t address,const void *data,size_t bytes,uint64_t lo,uint64_t hi) {
    if(!data || !bytes || bytes>0x100 || address<lo || address>=hi || bytes>hi-address) return 0;
#ifdef __linux__
    struct iovec local={(void *)data,bytes},remote={(void *)(uintptr_t)address,bytes};
    return process_vm_writev(getpid(),&local,1,&remote,1,0)==(ssize_t)bytes;
#else
    // This port's virtual reader is Linux-only. Never dereference a foreign descriptor.
    return 0;
#endif
}
