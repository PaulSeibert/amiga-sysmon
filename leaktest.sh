#!/bin/bash
# Make build/leaktest.adf, a memory-leak test disk. It boots, writes a report, runs the GUI
# twice for 5 s each, quits it with Ctrl-C (Break) each time, and writes a report after each
# run. Boot it with a folder mounted as Work:, then compare the "bytes free" lines in
# leak-before.txt, leak-mid.txt and leak-after.txt: they should be identical.
#
# Run, Wait and Break are copied from YOUR OWN Workbench 1.3 disk image (they are Commodore
# files and are not part of this repository). Point $WB13_ADF at it.
set -euo pipefail
cd "$(dirname "$0")"
WB13_ADF=${WB13_ADF:-'/mnt/j/amiga/disks/Workbench v1.3.3 rev 34.34 (1990)(Commodore)(A500-A2000)(Disk 1 of 2)(Workbench).adf'}
[ -f "$WB13_ADF" ] || { echo "set WB13_ADF to your Workbench 1.3 disk image" >&2; exit 1; }
[ -f build/sysmon ] || ./build.sh >/dev/null
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -v "$WB13_ADF:/wb.adf:ro" -w /src \
  "${AGK_TOOLCHAIN_IMAGE:-amigadev/crosstools:m68k-amigaos}" sh -euc "
  mkdir -p /tmp/c
  for f in Run Wait Break; do xdftool /wb.adf read c/\$f /tmp/c/\$f; done
  R='run sysmon\nwait 5\nbreak 2\nwait 3\n'
  printf \"sysmon >Work:leak-before.txt REPORT\n\${R}sysmon >Work:leak-mid.txt REPORT\n\${R}sysmon >Work:leak-after.txt REPORT\n\" > /tmp/ss
  rm -f build/leaktest.adf
  xdftool build/leaktest.adf format LeakTest ofs + boot install + write build/sysmon sysmon + makedir c \
    + write /tmp/c/Run c/Run + write /tmp/c/Wait c/Wait + write /tmp/c/Break c/Break \
    + makedir s + write /tmp/ss s/startup-sequence >/dev/null"
PLAY=${SYSMON_PLAY:-/mnt/j/amiga/play}
[ -d "$PLAY" ] && cp build/leaktest.adf "$PLAY/"
echo "ok: build/leaktest.adf"
