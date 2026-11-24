#!/usr/bin/env bash
# Copy the prepared WAVE OS SD content (~/Music/WaveOS_SD) onto the Cardputer's microSD card.
# Put the SD card in your PC first. Usage: copy_to_sd.sh [/path/to/sd]
set -e
SRC="$HOME/Music/WaveOS_SD/"
SD="${1:-}"
if [ -z "$SD" ]; then
  SD=$(lsblk -o MOUNTPOINT,RM -rn | awk '$2=="1" && $1!="" {print $1; exit}')
  [ -z "$SD" ] && SD=$(find "/run/media/$USER" "/media/$USER" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | head -n1)
fi
[ -z "$SD" ] || [ ! -d "$SD" ] && { echo "No SD card found. Insert it, or pass the path: $0 /run/media/$USER/XXXX"; exit 1; }
echo "Copying to $SD ..."
# music/ and playlists/ are mirrored exactly (old layout removed); everything else is only added
for d in music playlists; do
  mkdir -p "$SD/$d"
  rsync -rt --delete --modify-window=2 --info=progress2 "$SRC$d/" "$SD/$d/"
done
rsync -rt --modify-window=2 --exclude music --exclude playlists "$SRC" "$SD/"
rm -f "$SD/waveos/music.txt"  # old resume pointer may reference moved files
sync
echo "Done. Eject the card and put it back in the Cardputer."
