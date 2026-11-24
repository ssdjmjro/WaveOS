#!/usr/bin/env python3
"""Add new songs to an existing WAVE OS library WITHOUT touching what is already there.

  base:  ~/Music/WaveOS_SD/music_base      (copy of the SD card's /music)
  new:   ~/Music/mus for waveos            (or --new DIR)
  out:   ~/Music/WaveOS_SD/add/music/...   only the NEW files, already organized
         ~/Music/WaveOS_SD/add/playlists/  updated + new playlists

Songs that already exist (same artist + title) or appear twice (same YouTube id) are skipped.
"""
import argparse, glob, os, re, shutil, sys
from mutagen.id3 import ID3, TIT2, TPE1, TALB, TCON

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import organize_music as om


def norm(s):
    s = om.ascii_punct(s).lower()
    s = re.sub(r"\((?:[^()]*cover[^()]*)\)", "", s)  # ignore "(X cover)" suffixes when comparing
    return re.sub(r"[\W_]+", "", s)


def main():
    home = os.path.expanduser("~/Music")
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default=os.path.join(home, "WaveOS_SD/music_base"))
    ap.add_argument("--base-playlists", default=os.path.join(home, "WaveOS_SD/playlists_base"))
    ap.add_argument("--new", default=os.path.join(home, "mus for waveos"))
    ap.add_argument("--out", default=os.path.join(home, "WaveOS_SD/add"))
    args = ap.parse_args()
    shutil.rmtree(args.out, ignore_errors=True)
    os.makedirs(os.path.join(args.out, "music"))
    os.makedirs(os.path.join(args.out, "playlists"))

    rows = []         # (genre, artist, title, rel, raw_artist)
    keys = {}         # (artist, normtitle) -> rel
    titles = {}       # normtitle -> rel (for playlist matching)
    for f in sorted(glob.glob(os.path.join(args.base, "*", "*", "*.mp3"))):
        rel = "/music/" + os.path.relpath(f, args.base)
        genre, artist, fn = rel.split("/")[2:5]
        title = os.path.splitext(fn)[0]
        rows.append((genre, artist, title, rel, artist))
        keys[(artist.lower(), norm(title))] = rel
        titles.setdefault(norm(title), rel)
    base_count = len(rows)

    seen_id, added, skipped_same, skipped_dup = {}, [], [], 0
    for f in sorted(glob.glob(os.path.join(args.new, "*.mp3"))):
        try:
            tags = ID3(f)
        except Exception:
            tags = ID3()
        vid = (re.search(r"v=([\w-]{11})", str(tags.get("TXXX:purl", ""))) or [None, None])[1]
        if vid and vid in seen_id:
            skipped_dup += 1
            continue
        raw_title = str(tags.get("TIT2", "")) or re.sub(r"^\d+ - ", "", os.path.splitext(os.path.basename(f))[0])
        raw_artist = om.fix_artist(str(tags.get("TPE1", "")) or "Unknown", raw_title)
        artist = om.main_artist(raw_artist)
        title = om.cover_title(om.clean_title(raw_title, artist), artist)
        k = (artist.lower(), norm(title))
        if k in keys:  # exact same song is already on the card
            skipped_same.append(f"{artist} - {title}")
            if vid:
                seen_id[vid] = keys[k]
            continue
        genre = om.genre_of(artist)
        rel = f"/music/{om.safe(genre)}/{om.safe(artist)}/{om.safe(title)}.mp3"
        n = 2
        while any(r[3] == rel for r in rows):  # same name, different recording
            rel = f"/music/{om.safe(genre)}/{om.safe(artist)}/{om.safe(title)} ({n}).mp3"; n += 1
        dst = args.out + rel
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(f, dst)
        t = ID3(dst)
        for key in list(t.keys()):
            if key.startswith(("TXXX", "COMM", "TSSE")):
                del t[key]
        t.setall("TIT2", [TIT2(encoding=3, text=title)])
        t.setall("TPE1", [TPE1(encoding=3, text=om.ascii_punct(raw_artist))])
        t.setall("TALB", [TALB(encoding=3, text=genre)])
        t.setall("TCON", [TCON(encoding=3, text=genre)])
        t.save(dst, v2_version=3)
        rows.append((genre, artist, title, rel, raw_artist))
        keys[k] = rel
        titles.setdefault(norm(title), rel)
        if vid:
            seen_id[vid] = rel
        added.append(rel)

    # ---- playlists: keep the old ones, rebuild genre/smart ones over everything, add new ones
    pls = {}
    for g in sorted({r[0] for r in rows}):
        pls[g] = [r[3] for r in sorted(rows, key=lambda r: (r[1].lower(), r[2].lower())) if r[0] == g]
    for name, kws in om.SMART.items():
        pls[name] = [r[3] for r in rows if any(k in (r[4] + " " + r[1]).lower() for k in kws)]
    pls["New Additions"] = added
    order = os.path.join(os.path.dirname(os.path.abspath(__file__)), "playlist_order.txt")
    if os.path.exists(order):
        full = []
        for line in open(order, encoding="utf-8"):
            p = line.rstrip("\n").split("|")
            if len(p) < 3:
                continue
            rel = seen_id.get(p[1])
            if not rel:  # older songs: match by title
                nt = norm(re.sub(r"^\d+ - ", "", p[2]))
                rel = next((v for kt, v in titles.items() if len(kt) > 3 and (kt in nt)), None)
            if rel and rel not in full:
                full.append(rel)
        pls["My YouTube Playlist"] = full
    for name, paths in pls.items():
        if paths:
            with open(os.path.join(args.out, "playlists", om.safe(name) + ".m3u"), "w", encoding="utf-8") as fh:
                fh.write("#EXTM3U\n" + "".join(p + "\n" for p in paths))

    print(f"already on card: {base_count}   added: {len(added)}   skipped (same song already on card): {len(skipped_same)}   skipped (duplicate in folder): {skipped_dup}")
    for s in skipped_same:
        print("   = ", s)
    gc = {}
    for r in rows:
        gc[r[0]] = gc.get(r[0], 0) + 1
    print("library after:", sum(gc.values()), "songs |", ", ".join(f"{g} {n}" for g, n in sorted(gc.items())))
    print("playlists:", ", ".join(f"{k} ({len(v)})" for k, v in pls.items() if v))


if __name__ == "__main__":
    main()
