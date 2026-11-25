#!/bin/bash
# build + flash app partition + boot normally (no power cycle needed)
set -e
cd ~/WaveOS
~/bin/arduino-cli compile --fqbn "esp32:esp32:m5stack_cardputer:FlashSize=8M,PartitionScheme=default_8MB,USBMode=hwcdc,CDCOnBoot=cdc,DebugLevel=error" --build-path build WaveOS 2>&1 | grep -E "error|Sketch uses|Global" || true
test -f build/WaveOS.ino.bin
PORT=$(ls /dev/ttyACM* | head -1)
E=$(ls ~/.arduino15/packages/esp32/tools/esptool_py/*/esptool)
$E --chip esp32s3 --port $PORT --baud 460800 --before default-reset --after watchdog-reset write-flash -z 0x10000 build/WaveOS.ino.bin 2>&1 | grep -E "verified|rror"
sleep 6; ls /dev/ttyACM*
