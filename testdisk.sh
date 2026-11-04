#!/bin/bash
# Build a boot disk that runs a special SysMon build, for screenshots and recordings:
#   testdisk.sh about   -> build/sysmon-about.adf  (opens the About window at start)
#   testdisk.sh demo    -> build/sysmon-demo.adf   (drives itself; used for the README animation)
# Copied to $SYSMON_PLAY (default /mnt/j/amiga/play) if that folder exists.
set -euo pipefail
cd "$(dirname "$0")"
case "${1:-}" in
  about) FLAG=-DTEST_ABOUT ;;
  demo)  FLAG=-DDEMO ;;
  *) echo "usage: $0 about|demo" >&2; exit 1 ;;
esac
mkdir -p build
python3 tools/mkimage.py art/logo.txt logo build/logo.c
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -w /src \
  "${AGK_TOOLCHAIN_IMAGE:-amigadev/crosstools:m68k-amigaos}" sh -euc "
  m68k-amigaos-gcc -mcrt=nix13 -m68000 -Os -Wall -s $FLAG -Isrc -o /tmp/sysmon \
    src/main.c src/ui.c src/about.c src/sysinfo.c src/report.c build/logo.c
  printf 'sysmon\n' > /tmp/ss
  rm -f build/sysmon-$1.adf
  xdftool build/sysmon-$1.adf format SysMon ofs + boot install + write /tmp/sysmon sysmon \
    + makedir s + write /tmp/ss s/startup-sequence >/dev/null"
PLAY=${SYSMON_PLAY:-/mnt/j/amiga/play}
[ -d "$PLAY" ] && cp "build/sysmon-$1.adf" "$PLAY/"
echo "ok: build/sysmon-$1.adf"
