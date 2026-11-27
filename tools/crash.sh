#!/bin/bash
# decode the crash lines WAVE OS writes to /waveos/crash.txt:  tools/crash.sh /path/to/crash.txt
# (needs the ELF of the build that crashed, kept in elfs/ by build_flash.sh)
cd ~/WaveOS
A2L=$(ls ~/.arduino15/packages/esp32/tools/esp-x32/*/bin/xtensa-esp32s3-elf-addr2line | head -1)
grep -A2 "reset=" "${1:-crash.txt}" | while read -r line; do
  echo "$line"
  if [[ $line == elf=* ]]; then
    sha=$(echo "$line" | sed 's/elf=\([0-9a-f]*\).*/\1/')
    elf=elfs/$sha.elf
    [ -f "$elf" ] || { echo "    (no elfs/$sha.elf - built elsewhere)"; continue; }
    addrs=$(echo "$line" | grep -o '0x[0-9a-f]*' | awk '{ if (strtonum($1) >= 0x40000000) print }')
    $A2L -pfiaC -e "$elf" $addrs | sed 's/^/    /' | grep -v "?? ??:0"
  fi
done
