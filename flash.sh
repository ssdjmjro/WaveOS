#!/usr/bin/env bash
# WAVE OS: build, back up the current Cardputer firmware, then flash.
#   ./flash.sh            build + backup (first time only) + flash
#   ./flash.sh --no-build flash the existing build
#   ./flash.sh --restore  put the backed-up firmware back
set -euo pipefail
cd "$(dirname "$0")"
CLI=~/bin/arduino-cli
FQBN="esp32:esp32:m5stack_cardputer:FlashSize=8M,PartitionScheme=default_8MB,USBMode=hwcdc,CDCOnBoot=cdc,DebugLevel=error"
ESPTOOL=$(ls -d ~/.arduino15/packages/esp32/tools/esptool_py/*/esptool | tail -1)
BACKUP=backup/original_firmware_8MB.bin

echo "Waiting for the Cardputer on USB..."
PORT=""
for i in $(seq 1 ${WAIT_SECS:-900}); do
  PORT=$(ls /dev/ttyACM* 2>/dev/null | head -1 || true)
  [ -n "$PORT" ] && break
  sleep 1
done
[ -z "$PORT" ] && { echo "No device found (plug it in with a data USB-C cable)."; exit 1; }
echo "Found $PORT"
sleep 1

if [ "${1:-}" = "--restore" ]; then
  [ -f "$BACKUP" ] || { echo "No backup file found."; exit 1; }
  "$ESPTOOL" --chip esp32s3 --port "$PORT" --baud ${BAUD:-460800} write-flash 0x0 "$BACKUP"
  echo "Original firmware restored."; exit 0
fi

if [ "${1:-}" != "--no-build" ]; then
  $CLI compile -b "$FQBN" --output-dir build WaveOS
fi

if [ ! -f "$BACKUP" ]; then
  mkdir -p backup
  echo "Backing up the current firmware (8MB, ~2 min)..."
  "$ESPTOOL" --chip esp32s3 --port "$PORT" --baud ${BAUD:-460800} --after no-reset read-flash 0x0 0x800000 "$BACKUP.part" || { echo "BACKUP FAILED - not flashing."; exit 1; }
  mv "$BACKUP.part" "$BACKUP"
  sleep 1
  PORT=$(ls /dev/ttyACM* 2>/dev/null | head -1)
fi

echo "Flashing WAVE OS..."
"$ESPTOOL" --chip esp32s3 --port "$PORT" --baud ${BAUD:-460800} --before default-reset --after hard-reset \
  write-flash -z 0x0 build/WaveOS.ino.merged.bin
echo "Done! WAVE OS is on your Cardputer."
