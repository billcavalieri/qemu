/*
 * PowerPC 970 -> AArch64 direct execution path: executable memory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "jit.h"

#ifdef CONFIG_DARWIN
#include <mach/mach.h>

/* Not declared by the public headers; same prototype tcg/region.c uses. */
extern kern_return_t mach_vm_remap(vm_map_t target_task,
                                   mach_vm_address_t *target_address,
                                   mach_vm_size_t size,
                                   mach_vm_offset_t mask,
                                   int flags,
                                   vm_map_t src_task,
                                   mach_vm_address_t src_address,
                                   boolean_t copy,
                                   vm_prot_t *cur_protection,
                                   vm_prot_t *max_protection,
                                   vm_inherit_t inheritance);

bool ppc_arm64_region_init(PPCArm64Region *r, size_t size, Error **errp)
{
    mach_vm_address_t rx = 0;
    vm_prot_t cur_prot, max_prot;
    kern_return_t ret;
    void *rw;

    rw = mmap(NULL, size, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (rw == MAP_FAILED) {
        error_setg_errno(errp, errno, "ppc-arm64: cannot map %zu bytes", size);
        return false;
    }

    ret = mach_vm_remap(mach_task_self(), &rx, size, 0, VM_FLAGS_ANYWHERE,
                        mach_task_self(), (mach_vm_address_t)rw, false,
                        &cur_prot, &max_prot, VM_INHERIT_NONE);
    if (ret != KERN_SUCCESS) {
        error_setg(errp, "ppc-arm64: mach_vm_remap failed (%d)", (int)ret);
        munmap(rw, size);
        return false;
    }

    if (mprotect((void *)rx, size, PROT_READ | PROT_EXEC) != 0) {
        error_setg_errno(errp, errno, "ppc-arm64: mprotect of the RX view");
        munmap((void *)rx, size);
        munmap(rw, size);
        return false;
    }

    r->rw = rw;
    r->rx = (void *)rx;
    r->size = size;
    return true;
}
#else
bool ppc_arm64_region_init(PPCArm64Region *r, size_t size, Error **errp)
{
    error_setg(errp, "ppc-arm64: executable memory is only implemented "
                     "for Darwin hosts");
    return false;
}
#endif

void ppc_arm64_region_free(PPCArm64Region *r)
{
    if (r->rx) {
        munmap(r->rx, r->size);
    }
    if (r->rw) {
        munmap(r->rw, r->size);
    }
    memset(r, 0, sizeof(*r));
}
