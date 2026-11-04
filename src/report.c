/* Plain-text system report, shared by "sysmon REPORT" (CLI) and Project > Save report. */
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "sysinfo.h"
#include "report.h"

static struct Snapshot rs;   /* separate from the GUI's snapshot */

static void put(BPTR fh, const char *s)
{
    const char *e = s;
    while (*e)
        e++;
    Write(fh, (APTR)s, e - s);
}

static void entryline(BPTR fh, int view, const struct Entry *e)
{
    char line[120];
    switch (view) {
    case VIEW_TASKS:
        if (e->clinum)
            fmt(line, "  %4ld  %-6s %-4s %-24s CLI %ld %s\n", (LONG)e->pri, sysinfo_state(e->state),
                e->process ? "Proc" : "Task", e->name, (LONG)e->clinum, e->extra);
        else
            fmt(line, "  %4ld  %-6s %-4s %s\n", (LONG)e->pri, sysinfo_state(e->state),
                e->process ? "Proc" : "Task", e->name);
        break;
    case VIEW_PORTS:
        fmt(line, "  %-28s %s\n", e->name, e->extra);
        break;
    default:
        if (e->version < 0) {
            fmt(line, "  %-28s   -\n", e->name);
            break;
        }
        fmt(line, "  %-28s %3ld.%-4ld open %ld\n", e->name, (LONG)e->version, (LONG)e->revision,
            (LONG)e->opencnt);
    }
    put(fh, line);
}

void report_write(BPTR fh)
{
    static const char *const title[VIEW_COUNT] = {
        "Tasks (pri, state, type, name)", "Libraries", "Devices", "Resources", "Public message ports (owner)"
    };
    struct MemInfo m;
    struct HwInfo h;
    char line[120];
    int v, i;

    sysinfo_mem(&m);
    sysinfo_hw(&h);
    put(fh, "SysMon 1.0 system report\n"
            "Written by Paul Seibert, (c) 2026 Desert Palms Software. Freeware.\n\n");
    fmt(line, "Kickstart %ld.%ld  Exec %ld.%ld  CPU %s  FPU %s  Chipset %s  %s\n",
        (LONG)h.ksVer, (LONG)h.ksRev, (LONG)h.execVer, (LONG)h.execRev, h.cpu, h.fpu, h.chipset,
        h.pal ? "PAL" : "NTSC");
    put(fh, line);
    fmt(line, "Chip RAM  %6ldK free of %6ldK, largest block %6ldK  (%ld bytes free)\n",
        m.chipFree >> 10, m.chipTotal >> 10, m.chipLargest >> 10, m.chipFree);
    put(fh, line);
    fmt(line, "Fast RAM  %6ldK free of %6ldK, largest block %6ldK  (%ld bytes free)\n",
        m.fastFree >> 10, m.fastTotal >> 10, m.fastLargest >> 10, m.fastFree);
    put(fh, line);

    for (v = 0; v < VIEW_COUNT; v++) {
        sysinfo_gather(v, v == VIEW_TASKS ? SORT_PRI : SORT_NAME, &rs);
        fmt(line, "\n%s: %ld\n", title[v], (LONG)rs.total);
        put(fh, line);
        for (i = 0; i < rs.count; i++)
            entryline(fh, v, &rs.e[i]);
        if (rs.total > rs.count) {
            fmt(line, "  ... %ld more not shown\n", (LONG)(rs.total - rs.count));
            put(fh, line);
        }
    }
}
