/*
 * PowerPC 970 -> AArch64 direct execution path: entry point and gating.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/cacheflush.h"
#include "cpu.h"
#include "exec/icount.h"
#include "exec/translation-block.h"
#include "hw/core/cpu.h"
#include "jit.h"

/*
 * Refuse (return false) whenever TCG must see every instruction, or the
 * CPU is not the model this path is written for.
 */
static bool ppc_arm64_can_run(CPUState *cs, const TCGTBCPUState *s)
{
    CPUPPCState *env = cpu_env(cs);
    const uint32_t bad_cflags = CF_SINGLE_STEP | CF_NOIRQ | CF_COUNT_MASK |
                                CF_USE_ICOUNT | CF_BP_PAGE |
                                CF_NO_GOTO_TB | CF_NO_GOTO_PTR;

    if (s->cflags & bad_cflags) {
        return false;
    }
    if (icount_enabled() || cpu_single_stepping(cs)) {
        return false;
    }
    if (!QTAILQ_EMPTY(&cs->breakpoints) || !QTAILQ_EMPTY(&cs->watchpoints)) {
        return false;
    }
    if (env->excp_model != POWERPC_EXCP_970) {
        return false;
    }
    /* The Apple-mode 970 has no MSR[LE]; if it is ever set, leave to TCG. */
    if ((s->flags >> HFLAGS_LE) & 1) {
        return false;
    }
    return true;
}

#ifdef __aarch64__
/*
 * Phase 0 feasibility spike: prove that code written through the RW view
 * of a split mapping runs from the RX view inside this very binary, on a
 * vCPU thread.  Runs once; the result is only reported.
 */
static void ppc_arm64_spike(void)
{
    static const uint32_t code[] = {
        0xd2800540,     /* movz x0, #42 */
        0xd65f03c0,     /* ret */
    };
    PPCArm64Region r = { };
    Error *err = NULL;
    uint64_t (*fn)(void);
    uint64_t res;

    if (!ppc_arm64_region_init(&r, qemu_real_host_page_size(), &err)) {
        warn_report_err(err);
        return;
    }
    memcpy(r.rw, code, sizeof(code));
    flush_idcache_range((uintptr_t)r.rx, (uintptr_t)r.rw, sizeof(code));
    fn = (uint64_t (*)(void))r.rx;
    res = fn();
    info_report("ppc-arm64: executable-memory spike returned %" PRIu64
                " (expected 42): %s", res, res == 42 ? "ok" : "FAILED");
    ppc_arm64_region_free(&r);
}
#endif

bool ppc_try_arm64_exec(CPUState *cs, const TCGTBCPUState *s)
{
    if (!ppc_arm64_can_run(cs, s)) {
        return false;
    }

#ifdef __aarch64__
    {
        static gsize spike_once;

        if (g_once_init_enter(&spike_once)) {
            ppc_arm64_spike();
            g_once_init_leave(&spike_once, 1);
        }
    }
#endif

    /* No instruction is translated yet; TCG runs everything. */
    return false;
}
