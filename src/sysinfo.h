/* sysinfo: snapshots of Exec state (memory, hardware, system lists), V33-safe. */
#ifndef SYSINFO_H
#define SYSINFO_H

#include <exec/types.h>

#define MAX_ENTRIES 96
#define NAME_LEN    32

enum { VIEW_TASKS, VIEW_LIBS, VIEW_DEVS, VIEW_RES, VIEW_PORTS, VIEW_COUNT };
enum { SORT_NAME, SORT_PRI };

struct Entry {
    char  name[NAME_LEN];
    char  extra[NAME_LEN];   /* tasks: CLI command; ports: owner task */
    APTR  addr;              /* the Task / Library / MsgPort itself */
    ULONG sigwait;           /* tasks: signals waited for */
    WORD  version, revision; /* libraries, devices, resources */
    UWORD opencnt;
    BYTE  pri;
    UBYTE state;             /* tasks: TS_RUN / TS_READY / TS_WAIT */
    UBYTE process;           /* tasks: NT_PROCESS (1) or NT_TASK (0) */
    UBYTE clinum;            /* tasks: CLI number, 0 if none */
};

struct Snapshot {
    int count;
    int total;               /* entries found (may exceed MAX_ENTRIES) */
    struct Entry e[MAX_ENTRIES];
};

struct MemInfo {
    ULONG chipFree, chipTotal, chipLargest;
    ULONG fastFree, fastTotal, fastLargest;
};

struct HwInfo {
    UWORD ksVer, ksRev;      /* Kickstart ROM version (0 if unknown) */
    UWORD execVer, execRev;
    const char *cpu, *fpu, *chipset;
    BOOL  pal;
};

void sysinfo_mem(struct MemInfo *m);
void sysinfo_hw(struct HwInfo *h);
int  sysinfo_taskcount(void);
void sysinfo_gather(int view, int sort, struct Snapshot *s);
const char *sysinfo_state(UBYTE state);
/* Re-checks under Forbid() that t is still a live task named name, then acts on it. */
BOOL sysinfo_setpri(APTR t, const char *name, int pri, int *oldpri);
BOOL sysinfo_break(APTR t, const char *name);

/* sprintf via exec RawDoFmt: every numeric argument must be a LONG (%ld, %lx, %lc). */
void fmt(char *buf, const char *f, ...);

#endif
