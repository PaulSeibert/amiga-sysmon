/* SysMon user interface: one Workbench window, everything hand-built with V33 Intuition.
 *
 * Gadgets are hit areas only (no imagery); SysMon draws the tabs and buttons itself so
 * it can show the active tab and a ghosted (disabled) look without the 1.3 refresh
 * artifacts of border-only gadgets. The list scroller is a real proportional gadget. */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <graphics/gfxbase.h>
#include <graphics/gfxmacros.h>
#include <graphics/text.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include "sysinfo.h"
#include "report.h"
#include "about.h"
#include "ui.h"

extern struct GfxBase *GfxBase;

#define WIN_W     440
#define WIN_H     190
#define MIN_H     134
#define ROW_H     9
#define BAR_W     112
#define TAB_W     76
#define TAB_GAP   2
#define BTN_W     54
#define BTN_GAP   4
#define BTN_H     12
#define PROP_W    16
#define GRAPH_MAX 128
#define MSG_TICKS 4
#define PRI_LIMIT 20

enum { GID_TAB = 1, GID_PROP = 10, GID_BTN = 20 };
enum { BTN_PRIUP, BTN_PRIDN, BTN_BREAK, BTN_COUNT };

/* pens on the 1.3 Workbench screen */
enum { BLUE, WHITE, BLACK, ORANGE };

static struct TextAttr topaz8 = { (STRPTR)"topaz.font", 8, FS_NORMAL, FPF_ROMFONT };
static struct TextFont *font;
static struct Window *win;
static struct RastPort *rp;

static struct Gadget tabGad[VIEW_COUNT], btnGad[BTN_COUNT], propGad;
static struct PropInfo propInfo;
static struct Image knob;
static BOOL btnOn[BTN_COUNT];
static BOOL dragging;
static UWORD lastPot = 0xFFFF, lastBody;

static const char *const tabName[VIEW_COUNT] = { "Tasks", "Libraries", "Devices", "Resources", "Ports" };
static const char *const btnName[BTN_COUNT] = { "Pri +", "Pri -", "Break" };
static UWORD *ghost;   /* area pattern: the blitter reads it, so it must be in chip RAM */

static struct Snapshot snap;
static struct MemInfo mem;
static struct HwInfo hw;
static int view = VIEW_TASKS;
static int sortmode[VIEW_COUNT] = { SORT_PRI, SORT_NAME, SORT_NAME, SORT_NAME, SORT_NAME };
static int rate;                              /* index into rates[] */
static const ULONG rates[3] = { 1, 2, 5 };
static int top, sel = -1;
static APTR seladdr;
static char selname[NAME_LEN];
static char msg[48];
static int msgticks;

static UBYTE chipHist[GRAPH_MAX], fastHist[GRAPH_MAX];
static int histN;

/* layout, recomputed on resize */
static int X0, XR, yG1, yG2, yHw, yTabs, yHead, listL, listT, listB, rows, cols, btnY, gx, gy, gw, gh;

/* ---- menus ------------------------------------------------------------------------ */

enum { M_PROJECT, M_VIEW, M_OPTIONS };
#define N_PROJECT 3
#define N_VIEW    VIEW_COUNT
#define N_OPTIONS 5
static struct MenuItem projItem[N_PROJECT], viewItem[N_VIEW], optItem[N_OPTIONS];
static struct IntuiText projText[N_PROJECT], viewText[N_VIEW], optText[N_OPTIONS];
static struct Menu optMenu  = { NULL, 120, 0, 70, 0, MENUENABLED, (UBYTE *)"Options", optItem, 0, 0, 0, 0 };
static struct Menu viewMenu = { &optMenu, 70, 0, 50, 0, MENUENABLED, (UBYTE *)"View", viewItem, 0, 0, 0, 0 };
static struct Menu projMenu = { &viewMenu, 0, 0, 70, 0, MENUENABLED, (UBYTE *)"Project", projItem, 0, 0, 0, 0 };

static void mkitems(struct MenuItem *it, struct IntuiText *tx, int n, const char *const *label,
                    const char *keys, int width, UWORD flags, const LONG *excl)
{
    int i;
    for (i = 0; i < n; i++) {
        struct IntuiText t = { 0, 1, JAM2, (flags & CHECKIT) ? CHECKWIDTH : 2, 1, &topaz8,
                               (UBYTE *)label[i], NULL };
        struct MenuItem m = { i + 1 < n ? &it[i + 1] : NULL, 0, i * 10, width, 10,
                              ITEMTEXT | ITEMENABLED | HIGHCOMP | flags | (keys ? COMMSEQ : 0),
                              excl ? excl[i] : 0, &tx[i], NULL, keys ? keys[i] : 0, NULL, MENUNULL };
        tx[i] = t;
        it[i] = m;
    }
}

static void initmenus(void)
{
    static const char *const proj[] = { "About...", "Save report", "Quit" };
    static const char *const vw[] = { "Tasks", "Libraries", "Devices", "Resources", "Ports" };
    static const char *const opt[] = { "Refresh 1 sec", "Refresh 2 sec", "Refresh 5 sec",
                                       "Sort by name", "Sort by priority" };
    static const LONG vex[] = { 0x1E, 0x1D, 0x1B, 0x17, 0x0F };
    static const LONG oex[] = { 0x06, 0x05, 0x03, 0x10, 0x08 };
    mkitems(projItem, projText, N_PROJECT, proj, "ASQ", 8 * 11 + COMMWIDTH + 8, 0, NULL);
    mkitems(viewItem, viewText, N_VIEW, vw, "TLDRP", CHECKWIDTH + 8 * 9 + COMMWIDTH + 8, CHECKIT, vex);
    mkitems(optItem, optText, N_OPTIONS, opt, NULL, CHECKWIDTH + 8 * 16 + 4, CHECKIT, oex);
}

static void setcheck(struct MenuItem *it, BOOL on)
{
    if (on)
        it->Flags |= CHECKED;
    else
        it->Flags &= ~CHECKED;
}

static void syncmenus(void)
{
    int i;
    ClearMenuStrip(win);
    for (i = 0; i < N_VIEW; i++)
        setcheck(&viewItem[i], i == view);
    for (i = 0; i < 3; i++)
        setcheck(&optItem[i], i == rate);
    setcheck(&optItem[3], sortmode[view] == SORT_NAME);
    setcheck(&optItem[4], sortmode[view] == SORT_PRI);
    SetMenuStrip(win, &projMenu);
}

/* ---- small drawing helpers ---------------------------------------------------------- */

static int slen(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static void text(int x, int y, int fg, int bg, const char *s, int n)
{
    SetAPen(rp, fg);
    SetBPen(rp, bg);
    SetDrMd(rp, JAM2);
    Move(rp, x, y + rp->TxBaseline);
    Text(rp, (STRPTR)s, n);
}

/* s padded with spaces (or cut) to exactly n characters */
static void textpad(int x, int y, int fg, int bg, const char *s, int n)
{
    char buf[96];
    int i;
    if (n > (int)sizeof(buf))
        n = sizeof(buf);
    for (i = 0; i < n && s[i]; i++)
        buf[i] = s[i];
    for (; i < n; i++)
        buf[i] = ' ';
    text(x, y, fg, bg, buf, n);
}

static void fill(int x0, int y0, int x1, int y1, int pen)
{
    if (x1 < x0 || y1 < y0)
        return;
    SetAPen(rp, pen);
    RectFill(rp, x0, y0, x1, y1);
}

static void box(int x0, int y0, int x1, int y1, int pen)
{
    SetAPen(rp, pen);
    Move(rp, x0, y0);
    Draw(rp, x1, y0);
    Draw(rp, x1, y1);
    Draw(rp, x0, y1);
    Draw(rp, x0, y0);
}

static void kstr(char *d, ULONG bytes)
{
    ULONG k = bytes >> 10;
    if (k >= 10000)
        fmt(d, "%ldM", k >> 10);
    else
        fmt(d, "%ldK", k);
}

static void say(const char *m)
{
    int i;
    for (i = 0; m[i] && i < (int)sizeof(msg) - 1; i++)
        msg[i] = m[i];
    msg[i] = 0;
    msgticks = MSG_TICKS;
}

/* ---- layout -------------------------------------------------------------------------- */

static void layout(void)
{
    X0 = win->BorderLeft + 6;
    XR = win->Width - win->BorderRight - 7;
    yG1 = win->BorderTop + 4;
    yG2 = yG1 + 11;
    gx = X0 + 294;
    gy = yG1 - 2;
    gh = 23;
    gw = XR - gx + 1;
    if (gw - 2 > GRAPH_MAX) {
        gw = GRAPH_MAX + 2;
        gx = XR - gw + 1;
    }
    yHw = yG2 + 14;
    yTabs = yHw + 12;
    yHead = yTabs + 16;
    listT = yHead + 11;
    listL = X0;
    cols = (XR - PROP_W - 4 - listL) / 8;
    btnY = win->Height - win->BorderBottom - 4 - BTN_H;
    listB = btnY - 5;
    rows = (listB - listT + 1) / ROW_H;
    if (rows < 1)
        rows = 1;
}

static void initgad(struct Gadget *g, int id, int x, int y, int w, int h, UWORD flags, UWORD act, UWORD type)
{
    struct Gadget z = { 0 };
    *g = z;
    g->LeftEdge = x;
    g->TopEdge = y;
    g->Width = w;
    g->Height = h;
    g->Flags = flags;
    g->Activation = act;
    g->GadgetType = type;
    g->GadgetID = id;
}

static void addgadgets(void)
{
    int i;
    for (i = 0; i < VIEW_COUNT; i++) {
        initgad(&tabGad[i], GID_TAB + i, X0 + i * (TAB_W + TAB_GAP), yTabs, TAB_W, 12,
                GADGHCOMP, RELVERIFY, BOOLGADGET);
        AddGadget(win, &tabGad[i], (UWORD)~0);
    }
    for (i = 0; i < BTN_COUNT; i++) {
        initgad(&btnGad[i], GID_BTN + i, X0 + i * (BTN_W + BTN_GAP), btnY - win->Height, BTN_W, BTN_H,
                GADGHNONE | GRELBOTTOM, RELVERIFY, BOOLGADGET);
        AddGadget(win, &btnGad[i], (UWORD)~0);
    }
    propInfo.Flags = AUTOKNOB | FREEVERT;
    propInfo.VertBody = MAXBODY;
    propInfo.HorizBody = MAXBODY;
    /* spans the list box; GRELHEIGHT keeps it fitted when the window is resized */
    initgad(&propGad, GID_PROP, XR - PROP_W + 1, listT - 2, PROP_W,
            (listB + 2) - (listT - 2) + 1 - win->Height,
            GADGHNONE | GRELHEIGHT, RELVERIFY | GADGIMMEDIATE | FOLLOWMOUSE, PROPGADGET);
    propGad.GadgetRender = &knob;
    propGad.SpecialInfo = &propInfo;
    AddGadget(win, &propGad, (UWORD)~0);
    RefreshGadgets(win->FirstGadget, win, NULL);
}

/* ---- header: gauges, graph, hardware line --------------------------------------------- */

static int level(ULONG used, ULONG total, int range)
{
    ULONG t = total >> 10;
    if (!t)
        return 0;
    used >>= 10;
    if (used > t)
        used = t;
    return (int)(used * range / t);
}

static void drawgauge(int y, const char *label, ULONG freeb, ULONG total)
{
    int bx = X0 + 40, u;
    char a[12], b[12], line[24];
    text(X0, y, WHITE, BLUE, label, 4);
    box(bx - 1, y - 1, bx + BAR_W, y + 8, WHITE);
    u = level(total - freeb, total, BAR_W);
    fill(bx, y, bx + u - 1, y + 7, ORANGE);
    fill(bx + u, y, bx + BAR_W - 1, y + 7, BLACK);
    if (total) {
        kstr(a, freeb);
        kstr(b, total);
        fmt(line, "%s/%s free", a, b);
    } else
        fmt(line, "none");
    textpad(bx + BAR_W + 6, y, WHITE, BLUE, line, 16);
}

static void graphcol(int x, int i)
{
    int base = gy + gh - 2;
    fill(x, gy + 1, x, base, BLACK);
    if (chipHist[i])
        fill(x, base - chipHist[i] + 1, x, base, ORANGE);
    if (fastHist[i]) {
        SetAPen(rp, WHITE);
        WritePixel(rp, x, base - fastHist[i] + 1);
    }
}

static void drawgraph(void)
{
    int i, w = gw - 2;
    box(gx, gy, gx + gw - 1, gy + gh - 1, WHITE);
    fill(gx + 1, gy + 1, gx + gw - 2, gy + gh - 2, BLACK);
    for (i = 0; i < histN; i++)
        graphcol(gx + 1 + w - histN + i, i);
}

static void graphpush(void)
{
    int w = gw - 2, range = gh - 2;
    if (w > GRAPH_MAX)
        w = GRAPH_MAX;
    if (histN < w)
        histN++;
    else {
        int i;
        for (i = 0; i < w - 1; i++) {
            chipHist[i] = chipHist[i + 1];
            fastHist[i] = fastHist[i + 1];
        }
    }
    chipHist[histN - 1] = level(mem.chipTotal - mem.chipFree, mem.chipTotal, range);
    fastHist[histN - 1] = level(mem.fastTotal - mem.fastFree, mem.fastTotal, range);
    SetBPen(rp, BLACK);
    ScrollRaster(rp, 1, 0, gx + 1, gy + 1, gx + gw - 2, gy + gh - 2);
    SetBPen(rp, BLUE);
    graphcol(gx + gw - 2, histN - 1);
}

static void drawhw(void)
{
    char line[80], cpu[16];
    if (hw.fpu[0] == 'n')
        fmt(cpu, "%s", hw.cpu);
    else
        fmt(cpu, "%s+%s", hw.cpu, hw.fpu);
    if (hw.ksVer)
        fmt(line, "Kickstart %ld.%ld  %s  %s %s  Tasks: %ld", (LONG)hw.ksVer, (LONG)hw.ksRev, cpu,
            hw.chipset, hw.pal ? "PAL" : "NTSC", (LONG)sysinfo_taskcount());
    else
        fmt(line, "Exec %ld.%ld  %s  %s %s  Tasks: %ld", (LONG)hw.execVer, (LONG)hw.execRev, cpu,
            hw.chipset, hw.pal ? "PAL" : "NTSC", (LONG)sysinfo_taskcount());
    textpad(X0, yHw, WHITE, BLUE, line, (XR - X0 + 1) / 8);
}

static void drawmem(void)
{
    drawgauge(yG1, "Chip", mem.chipFree, mem.chipTotal);
    drawgauge(yG2, "Fast", mem.fastFree, mem.fastTotal);
}

/* ---- tabs, buttons, status -------------------------------------------------------------- */

static void drawtab(int i)
{
    int x = X0 + i * (TAB_W + TAB_GAP), y = yTabs, n = slen(tabName[i]);
    BOOL on = (i == view);
    fill(x + 1, y + 1, x + TAB_W - 2, y + 10, on ? ORANGE : BLUE);
    box(x, y, x + TAB_W - 1, y + 11, WHITE);
    text(x + (TAB_W - n * 8) / 2, y + 2, on ? BLACK : WHITE, on ? ORANGE : BLUE, tabName[i], n);
}

static BOOL wantbtn(int i)
{
    (void)i;
    return view == VIEW_TASKS && sel >= 0;
}

static void drawbtn(int i)
{
    int x = X0 + i * (BTN_W + BTN_GAP), y = btnY, n = slen(btnName[i]);
    fill(x + 1, y + 1, x + BTN_W - 2, y + BTN_H - 2, BLUE);
    box(x, y, x + BTN_W - 1, y + BTN_H - 1, WHITE);
    text(x + (BTN_W - n * 8) / 2, y + 2, WHITE, BLUE, btnName[i], n);
    if (!btnOn[i] && ghost) {
        SetDrMd(rp, JAM1);   /* JAM2 would paint the pattern's 0 bits too */
        SetAfPt(rp, ghost, 1);
        fill(x + 1, y + 1, x + BTN_W - 2, y + BTN_H - 2, BLUE);
        SetAfPt(rp, NULL, 0);
    }
}

/* Enable/disable = switch the gadget's highlighting; done with Remove/AddGadget as required. */
static void syncbuttons(BOOL force)
{
    int i;
    for (i = 0; i < BTN_COUNT; i++) {
        BOOL want = wantbtn(i);
        if (want != btnOn[i]) {
            UWORD pos = RemoveGadget(win, &btnGad[i]);
            btnGad[i].Flags = (btnGad[i].Flags & ~GADGHIGHBITS) | (want ? GADGHCOMP : GADGHNONE);
            AddGadget(win, &btnGad[i], pos);
            btnOn[i] = want;
            drawbtn(i);
        } else if (force)
            drawbtn(i);
    }
}

static void drawstatus(void)
{
    char line[64];
    int x = X0 + BTN_COUNT * (BTN_W + BTN_GAP) + 4;
    if (msgticks > 0)
        fmt(line, "%s", msg);
    else if (sel >= 0 && view == VIEW_TASKS)
        fmt(line, "$%08lx waits $%08lx", (ULONG)snap.e[sel].addr, snap.e[sel].sigwait);
    else if (sel >= 0)
        fmt(line, "$%08lx pri %ld", (ULONG)snap.e[sel].addr, (LONG)snap.e[sel].pri);
    else if (snap.total > snap.count)
        fmt(line, "%ld entries (%ld shown)", (LONG)snap.total, (LONG)snap.count);
    else
        fmt(line, "%ld entries", (LONG)snap.total);
    textpad(x, btnY + 2, msgticks > 0 ? ORANGE : WHITE, BLUE, line, (XR - x + 1) / 8);
}

/* ---- the list ------------------------------------------------------------------------------ */

/* append s into a column of width w (right-aligned if right) */
static char *col(char *d, const char *s, int w, BOOL right)
{
    int n = slen(s), i;
    if (n > w)
        n = w;
    if (right)
        for (i = n; i < w; i++)
            *d++ = ' ';
    for (i = 0; i < n; i++)
        *d++ = s[i];
    if (!right)
        for (i = n; i < w; i++)
            *d++ = ' ';
    *d++ = ' ';
    return d;
}

static void rowtext(char *buf, const struct Entry *e)
{
    char n[24], *d = buf;
    switch (view) {
    case VIEW_TASKS:
        d = col(d, e->name, 21, FALSE);
        fmt(n, "%ld", (LONG)e->pri);
        d = col(d, n, 4, TRUE);
        d = col(d, sysinfo_state(e->state), 5, FALSE);
        d = col(d, e->process ? "Proc" : "Task", 4, FALSE);
        if (e->clinum) {
            fmt(n, "%ld:%s", (LONG)e->clinum, e->extra[0] ? e->extra : "-");
            d = col(d, n, 16, FALSE);
        }
        break;
    case VIEW_PORTS:
        d = col(d, e->name, 24, FALSE);
        d = col(d, e->extra, 23, FALSE);
        break;
    default:
        d = col(d, e->name, 28, FALSE);
        if (e->version < 0) {
            d = col(d, "-", 8, TRUE);
            d = col(d, "-", 5, TRUE);
            break;
        }
        fmt(n, "%ld.%ld", (LONG)e->version, (LONG)e->revision);
        d = col(d, n, 8, TRUE);
        fmt(n, "%ld", (LONG)e->opencnt);
        d = col(d, n, 5, TRUE);
    }
    *d = 0;
}

static void drawhead(void)
{
    char buf[96], *d = buf;
    switch (view) {
    case VIEW_TASKS:
        d = col(d, "Name", 21, FALSE);
        d = col(d, "Pri", 4, TRUE);
        d = col(d, "State", 5, FALSE);
        d = col(d, "Type", 4, FALSE);
        d = col(d, "CLI:cmd", 16, FALSE);
        break;
    case VIEW_PORTS:
        d = col(d, "Port", 24, FALSE);
        d = col(d, "Signals task", 23, FALSE);
        break;
    default:
        d = col(d, "Name", 28, FALSE);
        d = col(d, "Version", 8, TRUE);
        d = col(d, "Open", 5, TRUE);
    }
    *d = 0;
    textpad(listL, yHead, ORANGE, BLUE, buf, cols);
}

static void drawlist(void)
{
    char buf[96];
    int r;
    for (r = 0; r < rows; r++) {
        int i = top + r, y = listT + r * ROW_H;
        if (i < snap.count) {
            BOOL s = (i == sel);
            rowtext(buf, &snap.e[i]);
            textpad(listL, y, s ? BLACK : WHITE, s ? ORANGE : BLUE, buf, cols);
            fill(listL, y + 8, listL + cols * 8 - 1, y + ROW_H - 1, s ? ORANGE : BLUE);
        } else
            fill(listL, y, listL + cols * 8 - 1, y + ROW_H - 1, BLUE);
    }
}

static void drawlistbox(void)
{
    box(listL - 2, listT - 2, listL + cols * 8 + 1, listB + 2, WHITE);
}

static void setprop(BOOL force)
{
    int over = snap.count - rows;
    UWORD body, pot;
    if (dragging)
        return;
    if (over <= 0) {
        body = MAXBODY;
        pot = 0;
    } else {
        body = (UWORD)((ULONG)rows * MAXBODY / snap.count);
        pot = (UWORD)((ULONG)top * MAXPOT / over);
    }
    if (force || pot != lastPot || body != lastBody) {
        NewModifyProp(&propGad, win, NULL, AUTOKNOB | FREEVERT, 0, pot, MAXBODY, body, 1);
        lastPot = pot;
        lastBody = body;
    }
}

static void topfromprop(void)
{
    int over = snap.count - rows, t = 0;
    if (over > 0)
        t = (int)(((ULONG)propInfo.VertPot * over + MAXPOT / 2) / MAXPOT);
    if (t != top) {
        top = t;
        drawlist();
    }
}

static void clamptop(void)
{
    if (top > snap.count - rows)
        top = snap.count - rows;
    if (top < 0)
        top = 0;
}

static void showsel(void)
{
    if (sel < 0)
        return;
    if (sel < top)
        top = sel;
    if (sel >= top + rows)
        top = sel - rows + 1;
}

static void regather(void)
{
    int i;
    sysinfo_gather(view, sortmode[view], &snap);
    sel = -1;
    for (i = 0; seladdr && i < snap.count; i++)
        if (snap.e[i].addr == seladdr) {
            sel = i;
            break;
        }
    if (sel < 0)
        seladdr = NULL;
    clamptop();
}

static void select(int i)
{
    sel = i;
    if (i >= 0) {
        seladdr = snap.e[i].addr;
        fmt(selname, "%s", snap.e[i].name);
    } else
        seladdr = NULL;
    msgticks = 0;
    showsel();
    drawlist();
    setprop(FALSE);
    syncbuttons(FALSE);
    drawstatus();
}

/* ---- whole-window drawing ------------------------------------------------------------------ */

static void drawall(void)
{
    int i;
    drawmem();
    drawgraph();
    drawhw();
    for (i = 0; i < VIEW_COUNT; i++)
        drawtab(i);
    drawhead();
    drawlistbox();
    drawlist();
    syncbuttons(TRUE);
    drawstatus();
    setprop(TRUE);
}

static void setview(int v)
{
    int old = view;
    if (v == view)
        return;
    view = v;
    drawtab(old);
    drawtab(view);
    top = 0;
    seladdr = NULL;
    regather();
    drawhead();
    drawlist();
    syncbuttons(FALSE);
    msgticks = 0;
    drawstatus();
    setprop(FALSE);
    syncmenus();
}

static void resized(void)
{
    layout();
    fill(win->BorderLeft, win->BorderTop, win->Width - win->BorderRight - 1,
         win->Height - win->BorderBottom - 1, BLUE);
    RefreshGadgets(win->FirstGadget, win, NULL);
    clamptop();
    showsel();
    drawall();
}

/* ---- requesters and actions ------------------------------------------------------------------ */

static BOOL request(const char *l1, const char *l2, const char *yes, const char *no)
{
    struct IntuiText b2 = { 0, 1, JAM2, 10, 16, &topaz8, (UBYTE *)l2, NULL };
    struct IntuiText b1 = { 0, 1, JAM2, 10, 5, &topaz8, (UBYTE *)l1, &b2 };
    struct IntuiText y = { 0, 1, JAM2, 6, 3, &topaz8, (UBYTE *)yes, NULL };
    struct IntuiText n = { 0, 1, JAM2, 6, 3, &topaz8, (UBYTE *)no, NULL };
    int w = slen(l1) > slen(l2) ? slen(l1) : slen(l2);
    return AutoRequest(win, &b1, yes ? &y : NULL, &n, 0, 0, w * 8 + 50, 70);
}

static void about(void)
{
    about_show(win, &topaz8);
}

static void savereport(void)
{
    BPTR fh = Open((STRPTR)"RAM:SysMon.report", MODE_NEWFILE);
    if (!fh) {
        say("Cannot write RAM:SysMon.report");
        return;
    }
    report_write(fh);
    Close(fh);
    say("Saved RAM:SysMon.report");
}

static void changepri(int delta)
{
    char m[48];
    int oldpri, pri = snap.e[sel].pri + delta;
    if (pri > PRI_LIMIT || pri < -PRI_LIMIT) {
        say("Limit is -20..+20");
        drawstatus();
        return;
    }
    if (sysinfo_setpri(seladdr, selname, pri, &oldpri)) {
        fmt(m, "Pri %ld -> %ld", (LONG)oldpri, (LONG)pri);
        say(m);
    } else
        say("That task has gone");
    regather();
    showsel();
    drawlist();
    setprop(FALSE);
    syncbuttons(FALSE);
    drawstatus();
}

static void sendbreak(void)
{
    char l2[48];
    fmt(l2, "%s?", selname);
    if (!request("Send Ctrl-C (break) to", l2, "Break", "Cancel"))
        return;
    say(sysinfo_break(seladdr, selname) ? "Ctrl-C sent" : "That task has gone");
    drawstatus();
}

/* ---- public ----------------------------------------------------------------------------------- */

BOOL ui_open(void)
{
    int h = WIN_H, y = 12, avail = GfxBase->NormalDisplayRows;
    struct NewWindow nw = {
        60, 12, WIN_W, WIN_H, 0, 1,
        CLOSEWINDOW | GADGETUP | GADGETDOWN | MOUSEMOVE | MOUSEBUTTONS | MENUPICK | NEWSIZE |
            REFRESHWINDOW | RAWKEY,
        WINDOWCLOSE | WINDOWDRAG | WINDOWDEPTH | WINDOWSIZING | SIZEBRIGHT | SMART_REFRESH | ACTIVATE,
        NULL, NULL, (UBYTE *)"SysMon 1.0", NULL, NULL, WIN_W, MIN_H, WIN_W, 0, WBENCHSCREEN
    };
    if (y + h > avail) {
        y = 0;
        h = avail > WIN_H ? WIN_H : avail;
    }
    nw.TopEdge = y;
    nw.Height = h;
    nw.MaxHeight = avail;
    if ((ghost = AllocMem(2 * sizeof(UWORD), MEMF_CHIP))) {
        ghost[0] = 0x8888;
        ghost[1] = 0x2222;
    }
    if (!(win = OpenWindow(&nw)))
        return FALSE;
    WindowLimits(win, WIN_W, MIN_H, WIN_W, win->WScreen->Height);
    rp = win->RPort;
    if ((font = OpenFont(&topaz8)))
        SetFont(rp, font);
    initmenus();
    syncmenus();
    layout();
    addgadgets();
    sysinfo_mem(&mem);
    sysinfo_hw(&hw);
    regather();
    drawall();
    graphpush();
#ifdef TEST_ABOUT
    about_show(win, &topaz8);   /* screenshot test build only */
#endif
    return TRUE;
}

void ui_close(void)
{
    if (win) {
        ClearMenuStrip(win);
        CloseWindow(win);
        win = NULL;
    }
    if (font)
        CloseFont(font);
    if (ghost)
        FreeMem(ghost, 2 * sizeof(UWORD));
    font = NULL;
    ghost = NULL;
}

ULONG ui_sigmask(void)
{
    return 1UL << win->UserPort->mp_SigBit;
}

ULONG ui_refresh_secs(void)
{
    return rates[rate];
}

#ifdef DEMO
/* Recording build only (-DDEMO): drives the GUI on its own for the README animation.
 * A 16-second loop: select tasks, visit the other views, re-sort, and allocate then free
 * memory so the gauges and graph move. Never part of a release build. */
#define DEMO_BLOCK (24 * 1024)
static void demostep(void)
{
    static int t;
    static APTR chipb[8], fastb[8];
    static int nb;
    t = (t % 16) + 1;
    if (t <= 8 && nb < 8) {
        chipb[nb] = AllocMem(DEMO_BLOCK, MEMF_CHIP);
        fastb[nb] = AllocMem(DEMO_BLOCK / 2, MEMF_FAST);
        nb++;
    } else if (t > 8 && nb > 0) {
        nb--;
        if (chipb[nb])
            FreeMem(chipb[nb], DEMO_BLOCK);
        if (fastb[nb])
            FreeMem(fastb[nb], DEMO_BLOCK / 2);
    }
    switch (t) {
    case 1:  setview(VIEW_TASKS); sortmode[VIEW_TASKS] = SORT_PRI; syncmenus(); break;
    case 2:  select(0); break;
    case 3:  select(2); break;
    case 4:  select(4); break;
    case 6:  setview(VIEW_LIBS); break;
    case 7:  select(3); break;
    case 9:  setview(VIEW_DEVS); break;
    case 10: select(5); break;
    case 12: setview(VIEW_RES); break;
    case 14: setview(VIEW_TASKS); sortmode[VIEW_TASKS] = SORT_NAME; syncmenus(); break;
    case 15: select(1); break;
    }
}
#endif

void ui_tick(void)
{
#ifdef DEMO
    demostep();
#endif
    if (msgticks > 0)
        msgticks--;
    sysinfo_mem(&mem);
    drawmem();
    graphpush();
    drawhw();
    regather();
    drawlist();
    setprop(FALSE);
    syncbuttons(FALSE);
    drawstatus();
}

static void menupick(UWORD code, BOOL *quit)
{
    int newview = -1, newsort = -1, i;
    while (code != MENUNULL) {
        struct MenuItem *it = ItemAddress(&projMenu, code);
        int m = MENUNUM(code), n = ITEMNUM(code);
        if (m == M_PROJECT) {
            if (n == 0)
                about();
            else if (n == 1)
                savereport();
            else
                *quit = TRUE;
        } else if (m == M_VIEW)
            newview = n;
        else if (m == M_OPTIONS) {
            if (n < 3)
                rate = n;
            else
                newsort = (n == 3) ? SORT_NAME : SORT_PRI;
        }
        code = it ? it->NextSelect : MENUNULL;
    }
    if (newsort >= 0 && newsort != sortmode[view]) {
        sortmode[view] = newsort;
        regather();
        showsel();
        drawlist();
        setprop(FALSE);
    }
    if (newview >= 0)
        setview(newview);
    else
        syncmenus();
    (void)i;
    drawstatus();
}

static void click(int mx, int my)
{
    int r;
    if (mx < listL || mx >= listL + cols * 8 || my < listT || my >= listT + rows * ROW_H)
        return;
    r = top + (my - listT) / ROW_H;
    select(r < snap.count ? r : -1);
}

static void key(UWORD code)
{
    int s;
    if (code & IECODE_UP_PREFIX || !snap.count)
        return;
    if (code == 0x4C)            /* cursor up */
        s = sel < 0 ? top : sel - 1;
    else if (code == 0x4D)       /* cursor down */
        s = sel < 0 ? top : sel + 1;
    else
        return;
    if (s < 0)
        s = 0;
    if (s >= snap.count)
        s = snap.count - 1;
    select(s);
}

BOOL ui_handle(void)
{
    struct IntuiMessage *im;
    BOOL quit = FALSE;
    while ((im = (struct IntuiMessage *)GetMsg(win->UserPort))) {
        ULONG class = im->Class;
        UWORD code = im->Code;
        WORD mx = im->MouseX, my = im->MouseY;
        struct Gadget *g = (struct Gadget *)im->IAddress;
        ReplyMsg((struct Message *)im);
        switch (class) {
        case CLOSEWINDOW:
            quit = TRUE;
            break;
        case GADGETDOWN:
            if (g->GadgetID == GID_PROP) {
                dragging = TRUE;
                topfromprop();
            }
            break;
        case MOUSEMOVE:
            if (dragging)
                topfromprop();
            break;
        case GADGETUP:
            if (g->GadgetID == GID_PROP) {
                topfromprop();
                dragging = FALSE;
                setprop(TRUE);
            } else if (g->GadgetID >= GID_TAB && g->GadgetID < GID_TAB + VIEW_COUNT) {
                setview(g->GadgetID - GID_TAB);
                drawtab(view);   /* the release complemented it */
            } else if (g->GadgetID >= GID_BTN && sel >= 0 && view == VIEW_TASKS) {
                int b = g->GadgetID - GID_BTN;
                if (b == BTN_PRIUP)
                    changepri(1);
                else if (b == BTN_PRIDN)
                    changepri(-1);
                else
                    sendbreak();
            }
            break;
        case MOUSEBUTTONS:
            if (code == SELECTDOWN)
                click(mx, my);
            break;
        case RAWKEY:
            key(code);
            break;
        case MENUPICK:
            menupick(code, &quit);
            break;
        case NEWSIZE:
            resized();
            break;
        case REFRESHWINDOW:
            BeginRefresh(win);
            EndRefresh(win, TRUE);
            break;
        }
    }
    return !quit;
}
