/* sysinfo: snapshots of Exec state. Only V33 (Kickstart 1.2/1.3) calls and fields.
 *
 * Rule: while the system lists are locked (Disable/Forbid) we only copy bytes into
 * the snapshot. No library call that could Wait, no drawing, no allocation. */
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <exec/ports.h>
#include <dos/dosextens.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <stdarg.h>
#include "sysinfo.h"

extern struct ExecBase *SysBase;
extern struct GfxBase *GfxBase;

/* RawDoFmt put-char routine: move.b d0,(a3)+ ; rts */
static const UWORD putch[] = { 0x16C0, 0x4E75 };

void fmt(char *buf, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    RawDoFmt((STRPTR)f, (APTR)ap, (void (*)())putch, buf);
    va_end(ap);
}

static void copyname(char *d, const char *s)
{
    int i = 0;
    if (!s)
        s = "(unnamed)";
    while (s[i] && i < NAME_LEN - 1) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
}

static void copybstr(char *d, BSTR b)
{
    const UBYTE *p = BADDR(b);
    int i, n = p ? p[0] : 0;
    if (n > NAME_LEN - 1)
        n = NAME_LEN - 1;
    for (i = 0; i < n; i++)
        d[i] = p[1 + i];
    d[n] = 0;
}

static struct Entry *newentry(struct Snapshot *s)
{
    struct Entry *e;
    s->total++;
    if (s->count >= MAX_ENTRIES)
        return NULL;
    e = &s->e[s->count++];
    e->name[0] = e->extra[0] = 0;
    e->addr = NULL;
    e->sigwait = 0;
    e->version = e->revision = 0;
    e->opencnt = 0;
    e->pri = 0;
    e->state = e->process = e->clinum = 0;
    return e;
}

/* ---- memory, hardware ---------------------------------------------------------- */

void sysinfo_mem(struct MemInfo *m)
{
    struct MemHeader *mh;
    m->chipTotal = m->fastTotal = 0;
    Forbid();
    for (mh = (struct MemHeader *)SysBase->MemList.lh_Head; mh->mh_Node.ln_Succ;
         mh = (struct MemHeader *)mh->mh_Node.ln_Succ) {
        ULONG size = (ULONG)mh->mh_Upper - (ULONG)mh->mh_Lower;
        if (mh->mh_Attributes & MEMF_CHIP)
            m->chipTotal += size;
        else
            m->fastTotal += size;
    }
    Permit();
    m->chipFree = AvailMem(MEMF_CHIP);
    m->chipLargest = AvailMem(MEMF_CHIP | MEMF_LARGEST);
    m->fastFree = AvailMem(MEMF_FAST);
    m->fastLargest = AvailMem(MEMF_FAST | MEMF_LARGEST);
}

void sysinfo_hw(struct HwInfo *h)
{
    UWORD attn = SysBase->AttnFlags, agnus;
    ULONG id = (ULONG)SysBase->LibNode.lib_IdString;
    const UWORD *rom = (const UWORD *)((id >= 0xFC0000 && id < 0x1000000) ? 0xFC0000 : 0xF80000);

    /* ROM header: $1111 or $1114, jmp, entry, $0000FFFF, version, revision */
    h->ksVer = h->ksRev = 0;
    if ((rom[0] == 0x1111 || rom[0] == 0x1114) && rom[1] == 0x4EF9) {
        h->ksVer = rom[6];
        h->ksRev = rom[7];
    }
    h->execVer = SysBase->LibNode.lib_Version;
    h->execRev = SysBase->LibNode.lib_Revision;

    h->cpu = (attn & 0x08) ? "68040" : (attn & 0x04) ? "68030" : (attn & 0x02) ? "68020"
           : (attn & 0x01) ? "68010" : "68000";
    h->fpu = (attn & 0x20) ? "68882" : (attn & 0x10) ? "68881"
           : ((attn & 0x08) && (attn & 0x40)) ? "68040" : "none";

    agnus = (*(volatile UWORD *)0xDFF004 >> 8) & 0x7F;   /* VPOSR: Agnus/Alice ID */
    h->chipset = (agnus & 0x0F) >= 2 ? "AGA" : (agnus & 0x20) ? "ECS" : "OCS";
    h->pal = (GfxBase->DisplayFlags & PAL) != 0;
}

/* ---- lists ---------------------------------------------------------------------- */

static void addtask(struct Snapshot *s, struct Task *t)
{
    struct Entry *e = newentry(s);
    if (!e)
        return;
    copyname(e->name, t->tc_Node.ln_Name);
    e->addr = t;
    e->pri = t->tc_Node.ln_Pri;
    e->state = t->tc_State;
    e->sigwait = t->tc_SigWait;
    if (t->tc_Node.ln_Type == NT_PROCESS) {
        struct Process *p = (struct Process *)t;
        e->process = 1;
        if (p->pr_CLI) {
            struct CommandLineInterface *cli = BADDR(p->pr_CLI);
            e->clinum = p->pr_TaskNum;
            if (cli->cli_Module)
                copybstr(e->extra, cli->cli_CommandName);
        }
    }
}

static void gathertasks(struct Snapshot *s)
{
    struct Node *n;
    /* Interrupts move tasks between TaskReady and TaskWait (Signal), so Disable(). */
    Disable();
    addtask(s, SysBase->ThisTask);
    for (n = SysBase->TaskReady.lh_Head; n->ln_Succ; n = n->ln_Succ)
        addtask(s, (struct Task *)n);
    for (n = SysBase->TaskWait.lh_Head; n->ln_Succ; n = n->ln_Succ)
        addtask(s, (struct Task *)n);
    Enable();
}

static void gatherlibs(struct Snapshot *s, struct List *l)
{
    struct Node *n;
    Forbid();
    for (n = l->lh_Head; n->ln_Succ; n = n->ln_Succ) {
        struct Library *lib = (struct Library *)n;
        struct Entry *e = newentry(s);
        if (!e)
            continue;
        copyname(e->name, n->ln_Name);
        e->addr = n;
        e->pri = n->ln_Pri;
        /* Some resources are a bare Node, not a Library: only trust plausible headers. */
        if (lib->lib_Version >= 1 && lib->lib_Version < 100 && lib->lib_Revision < 1000) {
            e->version = lib->lib_Version;
            e->revision = lib->lib_Revision;
            e->opencnt = lib->lib_OpenCnt;
        } else
            e->version = -1;
    }
    Permit();
}

static void gatherports(struct Snapshot *s)
{
    struct Node *n;
    Forbid();
    for (n = SysBase->PortList.lh_Head; n->ln_Succ; n = n->ln_Succ) {
        struct MsgPort *mp = (struct MsgPort *)n;
        struct Entry *e = newentry(s);
        if (!e)
            continue;
        copyname(e->name, n->ln_Name);
        e->addr = n;
        e->pri = n->ln_Pri;
        switch (mp->mp_Flags & PF_ACTION) {
        case PA_SIGNAL:
            if (mp->mp_SigTask)
                copyname(e->extra, ((struct Task *)mp->mp_SigTask)->tc_Node.ln_Name);
            break;
        case PA_SOFTINT:
            copyname(e->extra, "(software interrupt)");
            break;
        default:
            copyname(e->extra, "(ignored)");
        }
    }
    Permit();
}

static int nocase_cmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb || !ca)
            return ca - cb;
    }
}

static int before(const struct Entry *a, const struct Entry *b, int sort)
{
    if (sort == SORT_PRI && a->pri != b->pri)
        return a->pri > b->pri;
    return nocase_cmp(a->name, b->name) < 0;
}

static void sortsnap(struct Snapshot *s, int sort)
{
    int i, j;
    for (i = 1; i < s->count; i++) {
        struct Entry tmp = s->e[i];
        for (j = i; j > 0 && before(&tmp, &s->e[j - 1], sort); j--)
            s->e[j] = s->e[j - 1];
        s->e[j] = tmp;
    }
}

void sysinfo_gather(int view, int sort, struct Snapshot *s)
{
    s->count = s->total = 0;
    switch (view) {
    case VIEW_TASKS: gathertasks(s); break;
    case VIEW_LIBS:  gatherlibs(s, &SysBase->LibList); break;
    case VIEW_DEVS:  gatherlibs(s, &SysBase->DeviceList); break;
    case VIEW_RES:   gatherlibs(s, &SysBase->ResourceList); break;
    case VIEW_PORTS: gatherports(s); break;
    }
    sortsnap(s, sort);
}

int sysinfo_taskcount(void)
{
    struct Node *n;
    int c = 1;
    Disable();
    for (n = SysBase->TaskReady.lh_Head; n->ln_Succ; n = n->ln_Succ)
        c++;
    for (n = SysBase->TaskWait.lh_Head; n->ln_Succ; n = n->ln_Succ)
        c++;
    Enable();
    return c;
}

const char *sysinfo_state(UBYTE state)
{
    switch (state) {
    case TS_RUN:    return "Run";
    case TS_READY:  return "Ready";
    case TS_WAIT:   return "Wait";
    case TS_ADDED:  return "Added";
    case TS_EXCEPT: return "Except";
    case TS_REMOVED:return "Gone";
    default:        return "?";
    }
}

/* ---- task actions ------------------------------------------------------------------ */

static int samename(const char *a, const char *b)
{
    int i;
    if (!a)
        a = "(unnamed)";
    for (i = 0; i < NAME_LEN - 1; i++) {
        if (a[i] != b[i])
            return 0;
        if (!a[i])
            return 1;
    }
    return 1;
}

/* Call between Forbid() and Permit(): no task can exit while we hold it. */
static BOOL alive(struct Task *t, const char *name)
{
    struct Node *n;
    BOOL found = (t == SysBase->ThisTask);
    Disable();
    for (n = SysBase->TaskReady.lh_Head; !found && n->ln_Succ; n = n->ln_Succ)
        found = (n == (struct Node *)t);
    for (n = SysBase->TaskWait.lh_Head; !found && n->ln_Succ; n = n->ln_Succ)
        found = (n == (struct Node *)t);
    Enable();
    return found && samename(t->tc_Node.ln_Name, name);
}

BOOL sysinfo_setpri(APTR t, const char *name, int pri, int *oldpri)
{
    BOOL ok;
    Forbid();
    ok = alive(t, name);
    if (ok)
        *oldpri = SetTaskPri((struct Task *)t, pri);
    Permit();
    return ok;
}

BOOL sysinfo_break(APTR t, const char *name)
{
    BOOL ok;
    Forbid();
    ok = alive(t, name);
    if (ok)
        Signal((struct Task *)t, SIGBREAKF_CTRL_C);
    Permit();
    return ok;
}
