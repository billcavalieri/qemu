/*
 * Dynamic PowerPC instruction mix: counts executed instructions by
 * primary/extended opcode and by broad class (integer, branch, load/store,
 * scalar FP, AltiVec, system).  Used to size the work of a PPC -> AArch64
 * direct execution path.
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <glib.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static GMutex lock;
static GHashTable *table;       /* key -> Entry */
static int top_n = 60;

typedef struct {
    uint32_t key;
    const char *cls;
    struct qemu_plugin_scoreboard *count;
} Entry;

static const char *class_of(uint32_t po, uint32_t ext)
{
    switch (po) {
    case 4:  return "altivec";
    case 59: case 63:
        /* fp arithmetic, but 63 also holds mffs/mtfsf/fcmp/fsel etc. */
        return "fp";
    case 48: case 49: case 50: case 51: case 52: case 53: case 54: case 55:
        return "fp-ldst";
    case 16: case 18: return "branch";
    case 19:
        return (ext == 16 || ext == 528) ? "branch" : "system";
    case 31:
        switch (ext) {
        case 19: case 83: case 144: case 146: case 339: case 467: case 595:
        case 598: case 566: case 306: case 370: case 498: case 4: case 68:
        case 86: case 982: case 854: case 150: case 278: case 246: case 247:
        case 1014: case 470: case 54: case 758: case 342: case 311: case 439:
            return "system";
        case 23: case 55: case 87: case 119: case 279: case 311 + 1000:
        case 343: case 375: case 407: case 151: case 183: case 215: case 247 + 1000:
        case 20: case 21: case 53: case 84: case 149: case 181: case 244 + 1000:
        case 534: case 790: case 662: case 918: case 533: case 597: case 725:
        case 661: case 695: case 727: case 759: case 663: case 631 + 1000:
        case 535: case 567: case 599: case 631: case 855:
        case 7: case 39: case 103: case 135: case 167: case 231 + 1000:
        case 342 + 1000: case 52: case 276: case 502:
            return "ld/st";
        default: return "integer";
        }
    case 32: case 33: case 34: case 35: case 36: case 37: case 38: case 39:
    case 40: case 41: case 42: case 43: case 44: case 45: case 46: case 47:
    case 56: case 57: case 58: case 60: case 61: case 62:
        return "ld/st";
    case 17: return "system";
    case 2: case 3: return "system";
    default: return "integer";
    }
}

static void print_name(char *buf, size_t n, uint32_t po, uint32_t ext)
{
    static const char *prim[64] = {
        [7]="mulli",[8]="subfic",[10]="cmpli",[11]="cmpi",[12]="addic",
        [13]="addic.",[14]="addi",[15]="addis",[16]="bc",[17]="sc",[18]="b",
        [19]="xl-form",[20]="rlwimi",[21]="rlwinm",[23]="rlwnm",[24]="ori",
        [25]="oris",[26]="xori",[27]="xoris",[28]="andi.",[29]="andis.",
        [30]="rld*",[31]="x-form",[32]="lwz",[33]="lwzu",[34]="lbz",
        [35]="lbzu",[36]="stw",[37]="stwu",[38]="stb",[39]="stbu",[40]="lhz",
        [41]="lhzu",[42]="lha",[43]="lhau",[44]="sth",[45]="sthu",[46]="lmw",
        [47]="stmw",[48]="lfs",[49]="lfsu",[50]="lfd",[51]="lfdu",[52]="stfs",
        [53]="stfsu",[54]="stfd",[55]="stfdu",[58]="ld/ldu/lwa",
        [62]="std/stdu",[4]="altivec",[59]="fp-single",[63]="fp-double",
        [2]="tdi",[3]="twi",
    };
    static const struct { uint32_t po, ext; const char *n; } x[] = {
        {31,0,"cmp"},{31,32,"cmpl"},{31,266,"add"},{31,40,"subf"},
        {31,10,"addc"},{31,8,"subfc"},{31,138,"adde"},{31,136,"subfe"},
        {31,235,"mullw"},{31,491,"divw"},{31,459,"divwu"},{31,28,"and"},
        {31,444,"or"},{31,316,"xor"},{31,476,"nand"},{31,124,"nor"},
        {31,60,"andc"},{31,412,"orc"},{31,24,"slw"},{31,536,"srw"},
        {31,792,"sraw"},{31,824,"srawi"},{31,26,"cntlzw"},{31,954,"extsb"},
        {31,922,"extsh"},{31,23,"lwzx"},{31,87,"lbzx"},{31,279,"lhzx"},
        {31,343,"lhax"},{31,151,"stwx"},{31,215,"stbx"},{31,407,"sthx"},
        {31,20,"lwarx"},{31,150,"stwcx."},{31,339,"mfspr"},{31,467,"mtspr"},
        {31,371,"mftb"},{31,19,"mfcr"},{31,144,"mtcrf"},{31,83,"mfmsr"},
        {31,146,"mtmsr"},{31,598,"sync"},{31,854,"eieio"},{31,982,"icbi"},
        {31,1014,"dcbz"},{31,86,"dcbf"},{31,54,"dcbst"},{31,278,"dcbt"},
        {31,246,"dcbtst"},{31,306,"tlbie"},{31,566,"tlbsync"},{31,4,"tw"},
        {31,534,"lwbrx"},{31,662,"stwbrx"},{31,535,"lfsx"},{31,599,"lfdx"},
        {31,663,"stfsx"},{31,727,"stfdx"},{31,983,"stfiwx"},{31,103,"lvx"},
        {31,231,"stvx"},{31,7,"lvebx"},{31,38,"lvsl"},{31,6,"lvsl"},
        {31,215,"stbx"},{31,247,"stbux"},{31,119,"lbzux"},{31,55,"lwzux"},
        {31,183,"stwux"},{31,311,"lhzux"},{19,16,"bclr"},{19,528,"bcctr"},
        {19,150,"isync"},{19,50,"rfi"},{19,18,"rfid"},{19,257,"crand"},
        {19,449,"cror"},{19,193,"crxor"},{19,0,"mcrf"},
        {63,21,"fadd"},{63,20,"fsub"},{63,25,"fmul"},{63,18,"fdiv"},
        {63,29,"fmadd"},{63,28,"fmsub"},{63,30,"fnmsub"},{63,31,"fnmadd"},
        {63,72,"fmr"},{63,264,"fabs"},{63,40,"fneg"},{63,12,"frsp"},
        {63,15,"fctiwz"},{63,14,"fctiw"},{63,0,"fcmpu"},{63,32,"fcmpo"},
        {63,23,"fsel"},{63,22,"fsqrt"},{63,583,"mffs"},{63,711,"mtfsf"},
        {59,21,"fadds"},{59,20,"fsubs"},{59,25,"fmuls"},{59,18,"fdivs"},
        {59,29,"fmadds"},{59,28,"fmsubs"},{59,30,"fnmsubs"},{59,31,"fnmadds"},
        {59,24,"fres"},{59,26,"frsqrte"},
    };
    if (po == 31 || po == 19 || po == 63 || po == 59) {
        /* FP A-form uses only 5 extended bits: compare on those too */
        for (size_t i = 0; i < G_N_ELEMENTS(x); i++) {
            if (x[i].po == po && (x[i].ext == ext ||
                ((po == 59 || po == 63) && x[i].ext == (ext & 0x1f)
                 && x[i].ext < 32))) {
                snprintf(buf, n, "%s", x[i].n);
                return;
            }
        }
        snprintf(buf, n, "op%u/%u", po, ext);
        return;
    }
    if (po == 4) {
        snprintf(buf, n, "vmx/%u", ext);
        return;
    }
    snprintf(buf, n, "%s", prim[po] ? prim[po] : "other");
}

static void vcpu_tb_trans(struct qemu_plugin_tb *tb, void *udata)
{
    size_t n = qemu_plugin_tb_n_insns(tb);

    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint32_t w = 0;
        Entry *e;

        if (qemu_plugin_insn_size(insn) != 4) {
            continue;
        }
        qemu_plugin_insn_data(insn, &w, 4);
        w = GUINT32_FROM_BE(w);

        uint32_t po = w >> 26, ext;
        switch (po) {
        case 4:
            ext = (w & 0x3f) >= 32 ? (w & 0x3f) : (w & 0x7ff);
            break;
        case 59: case 63:
            ext = (w >> 1) & 0x3ff;
            break;
        case 19: case 31:
            ext = (w >> 1) & 0x3ff;
            break;
        default:
            ext = 0;
        }
        uint32_t key = (po << 12) | ext;

        g_mutex_lock(&lock);
        e = g_hash_table_lookup(table, GUINT_TO_POINTER(key));
        if (!e) {
            e = g_new0(Entry, 1);
            e->key = key;
            e->cls = class_of(po, ext);
            e->count = qemu_plugin_scoreboard_new(sizeof(uint64_t));
            g_hash_table_insert(table, GUINT_TO_POINTER(key), e);
        }
        g_mutex_unlock(&lock);

        qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
            insn, QEMU_PLUGIN_INLINE_ADD_U64,
            qemu_plugin_scoreboard_u64(e->count), 1);
    }
}

static gint cmp(gconstpointer a, gconstpointer b)
{
    const Entry *ea = a, *eb = b;
    uint64_t ca = qemu_plugin_u64_sum(qemu_plugin_scoreboard_u64(ea->count));
    uint64_t cb = qemu_plugin_u64_sum(qemu_plugin_scoreboard_u64(eb->count));
    return ca == cb ? 0 : (ca > cb ? -1 : 1);
}

static void plugin_exit(void *p)
{
    GList *l = g_hash_table_get_values(table);
    GHashTable *cls = g_hash_table_new(g_str_hash, g_str_equal);
    uint64_t total = 0;
    GString *out = g_string_new("");

    l = g_list_sort(l, cmp);
    for (GList *i = l; i; i = i->next) {
        Entry *e = i->data;
        uint64_t c = qemu_plugin_u64_sum(qemu_plugin_scoreboard_u64(e->count));
        uint64_t *pc = g_hash_table_lookup(cls, e->cls);
        if (!pc) {
            pc = g_new0(uint64_t, 1);
            g_hash_table_insert(cls, (gpointer)e->cls, pc);
        }
        *pc += c;
        total += c;
    }
    g_string_append_printf(out, "ppcmix: %" PRIu64 " instructions\n", total);
    GHashTableIter it;
    gpointer k, v;
    g_hash_table_iter_init(&it, cls);
    while (g_hash_table_iter_next(&it, &k, &v)) {
        g_string_append_printf(out, "  class %-8s %6.2f%%  %" PRIu64 "\n",
                               (char *)k, 100.0 * *(uint64_t *)v / total,
                               *(uint64_t *)v);
    }
    int n = 0;
    for (GList *i = l; i && n < top_n; i = i->next, n++) {
        Entry *e = i->data;
        uint64_t c = qemu_plugin_u64_sum(qemu_plugin_scoreboard_u64(e->count));
        char name[32];
        print_name(name, sizeof(name), e->key >> 12, e->key & 0xfff);
        g_string_append_printf(out, "  %-12s %-8s %6.2f%%  %" PRIu64 "\n",
                               name, e->cls, 100.0 * c / total, c);
    }
    qemu_plugin_outs(out->str);
    g_string_free(out, TRUE);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    for (int i = 0; i < argc; i++) {
        g_auto(GStrv) tokens = g_strsplit(argv[i], "=", 2);
        if (g_strcmp0(tokens[0], "top") == 0 && tokens[1]) {
            top_n = atoi(tokens[1]);
        }
    }
    table = g_hash_table_new(g_direct_hash, g_direct_equal);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans, NULL);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
