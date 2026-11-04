#!/bin/bash
# Build SysMon (AmigaOS 1.3+, 68000) into build/:
#   build/sysmon       the program          build/sysmon.info  its Workbench icon
#   build/sysmon.adf   bootable test disk: writes Work:sysmon-report.txt, then opens the window
# Needs Docker (amigadev/crosstools:m68k-amigaos: bebbo GCC + xdftool) and Python 3.
# If $SYSMON_SHARE (default /mnt/j/amiga/share, the emulator's Work: folder) exists,
# the results are copied there too.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
python3 tools/mkicon.py art/icon.txt build/sysmon.info 8192
python3 tools/mkimage.py art/logo.txt logo build/logo.c
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -w /src \
  "${AGK_TOOLCHAIN_IMAGE:-amigadev/crosstools:m68k-amigaos}" sh -euc '
  m68k-amigaos-gcc -mcrt=nix13 -m68000 -Os -Wall -Wextra -Wno-unused-parameter -s -Isrc \
    -o build/sysmon src/main.c src/ui.c src/about.c src/sysinfo.c src/report.c build/logo.c
  # AmigaDOS 1.3: redirection must come right after the command name
  printf "sysmon >Work:sysmon-report.txt REPORT\nsysmon\n" > build/startup-sequence
  rm -f build/sysmon.adf
  xdftool build/sysmon.adf format SysMon ofs + boot install + write build/sysmon sysmon \
    + write build/sysmon.info sysmon.info + makedir s + write build/startup-sequence s/startup-sequence >/dev/null'
SHARE=${SYSMON_SHARE:-/mnt/j/amiga/share}
[ -d "$SHARE" ] && cp build/sysmon build/sysmon.info build/sysmon.adf "$SHARE/"
ls -l build/sysmon build/sysmon.info build/sysmon.adf
