/*
 * PowerPC 970 -> AArch64 direct execution path (experimental).
 *
 * Runs common PowerPC instructions as generated ARM64 code and returns to
 * the TCG execution loop for everything else.  See the TCGCPUOps
 * try_native_exec hook in include/accel/tcg/cpu-ops.h.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PPC_ARM64_JIT_H
#define PPC_ARM64_JIT_H

#include "accel/tcg/tb-cpu-state.h"

/*
 * Return false to let TCG run the block at @s->pc unchanged.  Return true
 * only after executing at least one guest instruction, with nip and every
 * register already stored in CPUPPCState.
 */
bool ppc_try_arm64_exec(CPUState *cs, const TCGTBCPUState *s);

#ifdef __aarch64__
/*
 * Executable memory, mapped twice: written through @rw, executed from @rx
 * (the same technique TCG's split-wx mode uses on Darwin).  The two views
 * never need a pthread_jit_write_protect_np() toggle.
 */
typedef struct PPCArm64Region {
    void *rw;
    void *rx;
    size_t size;
} PPCArm64Region;

bool ppc_arm64_region_init(PPCArm64Region *r, size_t size, Error **errp);
void ppc_arm64_region_free(PPCArm64Region *r);
#endif

#endif /* PPC_ARM64_JIT_H */
