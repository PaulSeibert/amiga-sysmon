# SysMon: development notes

How SysMon is put together, what we learned writing it for Kickstart 1.3, and how it is tested.
For building and the release files, see the [README](../README.md).

## Design rules

- **V33 only.** It compiles against the Kickstart 1.3 includes (`-mcrt=nix13 -m68000`), so a call
  that is newer than 1.3 fails at build time instead of crashing a user's A500.
- **Never destabilise the system being watched.** While Exec's lists are locked
  (`Disable()`/`Forbid()`), SysMon only copies bytes into its own snapshot: no library call that
  could `Wait`, no drawing, no allocation. Everything is formatted and drawn afterwards.
- **Never kill tasks.** Removing a task is not safe on the Amiga. Pri +/- and Break (Ctrl-C) are
  the only actions, and before each one SysMon checks, under `Forbid()`, that the task still
  exists.
- **Quiet presentation**: the Workbench's own four colours, Topaz 8, no palette changes.

## Modules

| File | What it does |
|---|---|
| `src/main.c` | Startup, libraries, the event loop (window + timer.device + Ctrl-C), `REPORT` mode |
| `src/sysinfo.c` | Snapshots: memory (chip/fast, largest block), CPU/FPU, chipset, PAL/NTSC, and the Exec lists (tasks, libraries, devices, resources, ports). Also `fmt()`, a `sprintf` over exec's `RawDoFmt` |
| `src/ui.c` | The window: gauges, the rolling graph, tabs, the list with its scroller, buttons, menus |
| `src/report.c` | The plain-text report used by `sysmon REPORT` and Project > Save report |
| `src/about.c` | The About window with the Desert Palms logo (`art/logo.txt` → `build/logo.c`) |

## Kickstart 1.3 lessons from SysMon

- `RawDoFmt` needs `%ld` for every number (it reads WORDs for `%d`); `%lx` prints upper case.
- `PutStr` is V36: write with `Write(Output(), ...)`.
- Area-fill patterns (`SetAfPt`, used for the graph and for ghosting disabled buttons) must be
  in **chip RAM** and drawn in `JAM1`.
- Some nodes on `ResourceList` aren't `struct Library`; only read version fields when the node
  type says it is a library.
- The 2.0+ Workbench swaps the pen meanings (pen 0 grey, 1 black, 2 white, 3 blue; on 1.3 0 blue,
  1 white, 2 black, 3 orange). The logo and icons have plane data for both orders
  (`tools/mkimage.py`).
- 1.3 reports 200 display rows until the Workbench preferences load: open at a safe size and grow
  the window to the screen afterwards.
- A failing command in a 1.3 startup-sequence aborts the whole script: test disks start with
  `failat 21`.

## Tests

- `./testdisk.sh` builds the boot test disk: it writes `Work:sysmon-report.txt` (the `REPORT`
  output, checked on the PC) and opens the window. `./testdisk.sh about` and `demo` are the
  screenshot and animation builds.
- `./leaktest.sh` runs SysMon twice, stops it with Break each time and compares free memory
  before, between and after: on Kickstart 1.3 the byte counts match exactly.
- The test disks borrow `Run`, `Wait` and `Break` from your own Workbench 1.3 disk image
  (`WB13_ADF`); no Commodore files are in this repository.
- Tested under WinUAE: Kickstart 1.3 (A500, OCS, 512K + 512K) and 3.1 (ECS, 1MB + 1MB).
