/* SysMon 1.0: a live system monitor for AmigaOS 1.3 (V33) and later.
 * Written by Paul Seibert. (c) 2026 Desert Palms Software. Freeware.
 *   sysmon          open the monitor window (also from Workbench)
 *   sysmon REPORT   print a text report of the system to the CLI and exit */
#include <exec/types.h>
#include <exec/io.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <clib/alib_protos.h>
#include "sysinfo.h"
#include "report.h"
#include "ui.h"

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

static const char version[] __attribute__((used)) =
    "$VER: SysMon 1.0 (4.11.2026) (c) 2026 Desert Palms Software, by Paul Seibert. Freeware.";

static int isreport(const char *a)
{
    const char *w = "REPORT";
    for (; *a && *w; a++, w++)
        if ((*a & ~0x20) != *w)
            return 0;
    return !*a && !*w;
}

int main(int argc, char **argv)
{
    struct MsgPort *tport = NULL;
    struct timerequest *treq = NULL;
    BOOL timerOpen = FALSE, pending = FALSE, running = TRUE;
    int rc = 20;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 33);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 33);
    if (!IntuitionBase || !GfxBase)
        goto out;

    if (argc > 1) {
        if (!isreport(argv[1])) {
            static const char usage[] =                                /* PutStr() is V36 */
                "SysMon 1.0 - (c) 2026 Desert Palms Software, by Paul Seibert. Freeware.\n"
                "Usage: sysmon [REPORT]\n";
            Write(Output(), (APTR)usage, sizeof(usage) - 1);
            rc = 5;
            goto out;
        }
        report_write(Output());
        rc = 0;
        goto out;
    }

    if (!(tport = CreatePort(NULL, 0)) ||
        !(treq = (struct timerequest *)CreateExtIO(tport, sizeof(struct timerequest))))
        goto out;
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)treq, 0))
        goto out;
    timerOpen = TRUE;
    if (!ui_open())
        goto out;

    while (running) {
        ULONG sigs;
        if (!pending) {
            treq->tr_node.io_Command = TR_ADDREQUEST;
            treq->tr_time.tv_secs = ui_refresh_secs();
            treq->tr_time.tv_micro = 0;
            SendIO((struct IORequest *)treq);
            pending = TRUE;
        }
        sigs = Wait(ui_sigmask() | (1UL << tport->mp_SigBit) | SIGBREAKF_CTRL_C);
        if ((sigs & (1UL << tport->mp_SigBit)) && GetMsg(tport)) {
            pending = FALSE;
            ui_tick();
        }
        if (sigs & ui_sigmask())
            running = ui_handle();
        if (sigs & SIGBREAKF_CTRL_C)
            running = FALSE;
    }
    rc = 0;

out:
    ui_close();
    if (pending) {
        AbortIO((struct IORequest *)treq);
        WaitIO((struct IORequest *)treq);
    }
    if (timerOpen)
        CloseDevice((struct IORequest *)treq);
    if (treq)
        DeleteExtIO((struct IORequest *)treq);
    if (tport)
        DeletePort(tport);
    if (GfxBase)
        CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
    return rc;
}
