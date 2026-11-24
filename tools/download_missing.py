#!/usr/bin/env python3
"""Download missing songs from the playlist and update channels.json and WaveOS_SD."""

import argparse
import json
import os
import re
import subprocess
import sys
import time
from mutagen.id3 import ID3

PLAYLIST_TXT = "/home/ssdjmjro/WaveOS/debug/playlist.txt"
MUSIC_DIR = "/home/ssdjmjro/Music"
SHUFFLED_DIR = os.path.join(MUSIC_DIR, "YouTube_Playlist_Shuffled")
CHANNELS_JSON = "/home/ssdjmjro/WaveOS/tools/channels.json"
STATUS_FILE = "/home/ssdjmjro/WaveOS/debug/download_status.json"
LOG_FILE = "/home/ssdjmjro/WaveOS/debug/download.log"

def get_existing_ids():
    have = set()
    import glob
    for f in glob.glob(os.path.join(MUSIC_DIR, "YouTube_Playlist_*/*.mp3")):
        try:
            tags = ID3(f)
            u = str(tags.get("TXXX:purl", "")) or str(tags.get("COMM:ID3v1 Comment:eng", ""))
            m = re.search(r"v=([\w-]{11})", u)
            if m:
                have.add(m.group(1))
        except Exception:
            pass
    return have

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--limit", type=int, default=0, help="Limit number of songs to download (0 = all)")
    args = ap.parse_args()

    have = get_existing_ids()
    rows = []
    with open(PLAYLIST_TXT, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line.count("|") >= 3:
                rows.append(line.split("|", 3))

    missing = [r for r in rows if r[1] not in have]
    # Deduplicate missing by video ID
    seen_ids = set()
    unique_missing = []
    for r in missing:
        if r[1] not in seen_ids:
            seen_ids.add(r[1])
            unique_missing.append(r)

    if args.limit > 0:
        unique_missing = unique_missing[:args.limit]

    total = len(unique_missing)
    print(f"Total missing to download: {total}")

    # Load existing channels.json
    channels = {}
    if os.path.exists(CHANNELS_JSON):
        with open(CHANNELS_JSON, "r", encoding="utf-8") as f:
            channels = json.load(f)

    # yt-dlp binary
    ytdlp = os.path.expanduser("~/.local/bin/yt-dlp")

    # Output template: 001 - Title.mp3 (using playlist index)
    out_tmpl = os.path.join(SHUFFLED_DIR, "%(playlist_index)03d - %(title)s.%(ext)s")

    success_count = 0
    fail_count = 0

    with open(LOG_FILE, "a", encoding="utf-8") as log:
        log.write(f"\n--- Starting download batch at {time.strftime('%Y-%m-%d %H:%M:%S')} ({total} songs) ---\n")
        log.flush()

        for idx, (pl_idx, vid, title, channel) in enumerate(unique_missing, start=1):
            status = {
                "current": idx,
                "total": total,
                "current_title": title,
                "current_channel": channel,
                "current_vid": vid,
                "success": success_count,
                "failed": fail_count,
                "timestamp": time.time()
            }
            with open(STATUS_FILE, "w", encoding="utf-8") as sf:
                json.dump(status, sf, indent=2)

            log.write(f"[{idx}/{total}] Downloading {vid}: {title} ({channel})...\n")
            log.flush()

            t_start = time.time() - 2.0
            cmd = [
                ytdlp,
                "--no-update",
                "--no-warnings",
                "--extract-audio",
                "--audio-format", "mp3",
                "--audio-quality", "0",
                "--embed-thumbnail",
                "--add-metadata",
                "--output", os.path.join(SHUFFLED_DIR, f"{pl_idx} - %(title)s.%(ext)s"),
                f"https://www.youtube.com/watch?v={vid}"
            ]

            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            if res.returncode == 0:
                success_count += 1
                # Find downloaded file by mtime
                import glob
                all_mp3s = glob.glob(os.path.join(SHUFFLED_DIR, "*.mp3"))
                new_files = [f for f in all_mp3s if os.path.getmtime(f) >= t_start]
                for nf in new_files:
                    fname = os.path.basename(nf)
                    key = f"YouTube_Playlist_Shuffled/{fname}"
                    channels[key] = channel
                if new_files:
                    with open(CHANNELS_JSON, "w", encoding="utf-8") as f:
                        json.dump(channels, f, indent=1, ensure_ascii=False)
                log.write(f"  -> SUCCESS: {title}\n")
            else:
                fail_count += 1
                log.write(f"  -> FAILED: {title}\nOutput: {res.stdout[-300:]}\n")
            log.flush()

            # Slight delay to be polite to YouTube
            time.sleep(1.0)

    # Final status
    final_status = {
        "current": total,
        "total": total,
        "completed": True,
        "success": success_count,
        "failed": fail_count,
        "timestamp": time.time()
    }
    with open(STATUS_FILE, "w", encoding="utf-8") as sf:
        json.dump(final_status, sf, indent=2)

    print(f"Finished: {success_count} succeeded, {fail_count} failed.")

    # Automatically organize music library into WaveOS_SD
    print("Running organize_music.py...")
    subprocess.run(["python3", "/home/ssdjmjro/WaveOS/tools/organize_music.py"], check=False)
    print("WaveOS_SD library updated.")

    # Check if SD card is mounted
    sd_check = subprocess.run(
        "lsblk -o MOUNTPOINT,RM -rn | awk '$2==\"1\" && $1!=\"\" {print $1; exit}'",
        shell=True, stdout=subprocess.PIPE, text=True
    ).stdout.strip()
    if not sd_check:
        import glob
        media_dirs = glob.glob(f"/media/{os.environ.get('USER', 'ssdjmjro')}/*") + glob.glob(f"/run/media/{os.environ.get('USER', 'ssdjmjro')}/*")
        if media_dirs:
            sd_check = media_dirs[0]

    if sd_check and os.path.isdir(sd_check):
        print(f"SD card found at {sd_check}. Flashing to SD...")
        subprocess.run(["/home/ssdjmjro/WaveOS/tools/copy_to_sd.sh", sd_check], check=False)
        print("Flashing to SD card complete!")
    else:
        print("SD card not detected on PC yet. Once you plug in the SD card, run: ~/WaveOS/tools/copy_to_sd.sh")

if __name__ == "__main__":
    main()
