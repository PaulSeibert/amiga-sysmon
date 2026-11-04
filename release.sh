#!/bin/bash
# Build the release archive dist/SysMon.lha (runs ./build.sh first):
#   SysMon.info              drawer icon
#   SysMon/SysMon            the program        SysMon/SysMon.info         its icon
#   SysMon/SysMon.readme     Aminet readme      SysMon/SysMon.readme.info  opens it with More
# Also puts SysMon.readme (Aminet wants it next to the archive) and sysmon.adf in dist/.
set -euo pipefail
cd "$(dirname "$0")"
./build.sh >/dev/null
rm -rf dist
mkdir -p dist/SysMon
cp build/sysmon dist/SysMon/SysMon
cp build/sysmon.info dist/SysMon/SysMon.info
cp SysMon.readme dist/SysMon/SysMon.readme
python3 tools/mkicon.py --drawer art/drawer.txt dist/SysMon.info
python3 tools/mkicon.py --project SYS:Utilities/More art/readme-icon.txt dist/SysMon/SysMon.readme.info
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD/dist:/dist" -w /dist \
  "${AGK_TOOLCHAIN_IMAGE:-amigadev/crosstools:m68k-amigaos}" sh -euc '
  lha -aq SysMon.lha SysMon.info SysMon/SysMon SysMon/SysMon.info SysMon/SysMon.readme SysMon/SysMon.readme.info
  lha -l SysMon.lha'
rm -rf dist/SysMon dist/SysMon.info
cp SysMon.readme build/sysmon.adf dist/
SHARE=${SYSMON_SHARE:-/mnt/j/amiga/share}
[ -d "$SHARE" ] && cp dist/SysMon.lha "$SHARE/"
ls -l dist
