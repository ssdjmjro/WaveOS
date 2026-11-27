#!/bin/bash
# build + flash app partition + boot normally (no power cycle needed)
set -e
cd ~/WaveOS
mkdir -p debug build
~/bin/arduino-cli compile --fqbn "esp32:esp32:m5stack_cardputer:FlashSize=8M,PartitionScheme=default_8MB,USBMode=hwcdc,CDCOnBoot=cdc,DebugLevel=error" --build-path build WaveOS > debug/compile.log 2>&1 || { grep -E "error" debug/compile.log; echo "BUILD FAILED"; exit 1; }
grep -E "Sketch uses|Global" debug/compile.log
# keep this build's ELF (named by the hash the crash dump records) so tools/crash.sh can decode crashes later
mkdir -p elfs
SHA=$(python3 -c "d=open('build/WaveOS.ino.bin','rb').read(); i=d.find(bytes.fromhex('3254cdab')); print(d[i+0x90:i+0x98].hex())")
cp build/WaveOS.ino.elf elfs/$SHA.elf
PORT=$(ls /dev/ttyACM* | head -1)
E=$(ls ~/.arduino15/packages/esp32/tools/esptool_py/*/esptool)
$E --chip esp32s3 --port $PORT --baud 460800 --before default-reset --after watchdog-reset write-flash -z 0x10000 build/WaveOS.ino.bin 2>&1 | grep -E "verified|rror"
sleep 6; ls /dev/ttyACM*
