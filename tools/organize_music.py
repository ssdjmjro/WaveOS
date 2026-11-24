#!/usr/bin/env python3
"""WAVE OS music organizer.

Takes the MP3s in ~/Music/YouTube_Playlist_* and builds a clean SD-card layout:

  music/<Genre>/<Artist>/<Title>.mp3     (tidy names, duplicates removed)
  playlists/*.m3u                        (original playlists + smart playlists)

Usage:
  organize_music.py                 -> writes to ~/Music/WaveOS_SD (copy that to the SD card root)
  organize_music.py --sd /media/... -> writes straight onto a mounted SD card
"""
import argparse, glob, json, os, re, shutil, unicodedata
from mutagen.id3 import ID3, TIT2, TPE1, TALB, TCON, TRCK

GENRES = {
    "Rock": ["Nirvana", "Guns N' Roses", "Queen", "AC-DC", "Van Halen", "Eagles", "Lynyrd Skynyrd", "Chuck Berry",
             "The Jimi Hendrix Experience", "Slipknot", "Led Zeppelin", "Derek & The Dominos", "Dire Straits",
             "Radiohead", "Ozzy Osbourne", "The White Stripes", "Metallica", "The Police"],
    "Guitar": ["Polyphia", "Ichika Nito", "Tim Henson", "Danny Yau", "Sungha Jung", "Manuel Gardner Fernandes",
               "Marcin", "John Mayer", "Zenith Akirae", "Seven Kayne", "福島拓人", "TACT", "Kotaro Oshio", "BERNTH",
               "Alexandr Misko", "sundial", "Tommy Emmanuel", "Steve Vai", "Depapepe", "Shin Izumi", "Iqbal Gumilar",
               "AironA", "Jinsan Kim", "Davie504", "Showmain", "Leo Birenberg"],
    "K-Pop": ["BABYMONSTER", "BIGBANG", "G-DRAGON", "GD", "Stray Kids", "aespa", "(G)I-DLE", "BLACKPINK", "ILLIT",
              "TAEYANG", "T.O.P", "LE SSERAFIM", "CORTIS", "IVE", "DAYOUNG", "PSY", "HUNTR/X", "MEOVV", "SUPER JUNIOR",
              "DAESUNG", "KiiiKiii", "YENA", "NMIXX", "ASMRZ", "LILY"],
    "J-Pop": ["Fujii Kaze", "YiNoYume", "QUEENDOM"],
    "Pop": ["AJR", "Ariana Grande", "Celtic Woman", "RJ Pasin"],
}
# uploader / label channels: the real artist is taken from the video title instead
LABELS = {"jyp entertainment", "smtown", "saintives", "mystic plug records ii", "starship", "stone music", "kcon official",
          "ppw", "namolla family hotshow", "the first take", "hybe labels", "riserecords"}
ALIAS = {"ichika": "Ichika Nito", "stevevaihimself": "Steve Vai", "officialpsy": "PSY", "i-dle (아이들)": "(G)I-DLE",
         "jin san kim 김진산": "Jinsan Kim", "showmaîn邱小明": "Showmain", "officialgdragon": "G-DRAGON", "yinoyume🪭": "YiNoYume",
         "depapepe": "Depapepe", "kotaro oshio, depapepe": "Kotaro Oshio", "平民防空洞": "QUEENDOM"}


def artist_from_title(title):
    t = ascii_punct(title)
    t = re.sub(r"^\[[^\]]*\]\s*", "", t)                      # [MV] / [KCON ...]
    m = re.match(r"^\s*([^'\"(\-]+?)\s*(?:\(([^)]*)\))?\s*(?:-|'|\")", t)
    if not m:
        return None
    a = m.group(1).strip()
    if re.search(r"[A-Za-z]", m.group(2) or "") and not re.search(r"[A-Za-z]", a):
        a = m.group(2)                                             # 대성(DAESUNG) -> DAESUNG
    a = re.sub(r"\s*\((?:[^()]|\([^)]*\))*\)\s*$", "", a)          # strip trailing (Korean name)
    a = re.sub(r"\s+[^\x00-\x7f].*$", "", a).strip()                # SUPER JUNIOR 슈퍼주니어 -> SUPER JUNIOR
    if a.lower().startswith("(여자)아이들") or "(g)i-dle" in t.lower()[:30]:
        return "(G)I-DLE"
    return a or None


def fix_artist(raw, title):
    r = _fix_artist(raw, title)
    if r.upper().startswith("LILY SULLYOON"):
        r = "NMIXX"
    return re.sub(r"\s+", " ", r).strip()


def _fix_artist(raw, title):
    a = ascii_punct(raw).strip()
    low = a.lower()
    if low in ALIAS:
        return ALIAS[low]
    first = re.split(r",\s*", a)[0].strip()
    if low in LABELS or first.lower() in LABELS:
        rest = [x.strip() for x in re.split(r",\s*", a)[1:] if x.strip().lower() not in LABELS]
        return rest[0] if rest else (artist_from_title(title) or a)
    if first.lower() in ALIAS:
        return ALIAS[first.lower()]
    return a


def cover_title(title, artist):
    """'(Michael Jackson) Billie Jean - Sungha Jung (Guitarlele)' -> 'Billie Jean (Guitarlele) (Michael Jackson cover)'"""
    t = title
    t = re.sub(r"\s*-\s*" + re.escape(artist) + r"\b", "", t, flags=re.I).strip()
    m = re.match(r"^\(([^)]+)\)\s*(.+)$", t)
    if m:
        orig, rest = m.group(1).strip(), m.group(2).strip()
        rest = rest.replace("_", " ")
        if orig.lower() in ("original", artist.lower()):
            return rest
        return f"{rest} ({orig} cover)"
    return t.replace("_", " ")
SMART = {  # extra playlists by artist keyword
    "Sungha Jung": ["sungha jung"],
    "Polyphia & Tim Henson": ["polyphia", "tim henson"],
    "Ichika Nito": ["ichika"],
    "Nirvana": ["nirvana"],
    "BABYMONSTER": ["babymonster"],
    "BIGBANG Family": ["bigbang", "g-dragon", "taeyang", "t.o.p", "gd"],
}
JUNK = re.compile(r"\s*[\(\[]\s*(official|audio|visualizer|lyric|lyrics|hd|4k|m/?v|mv|live clip|remaster|video)[^\)\]]*[\)\]]", re.I)


def ascii_punct(s):
    table = {"⧸": "-", "／": "-", "：": " -", "＂": "'", "｜": "-", "＆": "&", "　": " ", "’": "'", "‘": "'", "“": "'", "”": "'", "—": "-", "–": "-"}
    for a, b in table.items():
        s = s.replace(a, b)
    return unicodedata.normalize("NFC", s)


def safe(name):
    name = re.sub(r'[\\/:*?"<>|]', "-", ascii_punct(name))
    return re.sub(r"\s+", " ", name).strip(" .-")[:80] or "Untitled"


def clean_title(t, artist):
    t = ascii_punct(t)
    t = JUNK.sub("", t)
    t = re.sub(r"\b(M-?V|MV|Official (Music )?Video|Official Audio)\b", "", t, flags=re.I)
    if artist and t.lower().startswith(artist.lower() + " - "):
        t = t[len(artist) + 3:]
    t = t.strip(" '\"-")
    return re.sub(r"\s+", " ", t) or "Untitled"


def main_artist(a):
    a = ascii_punct(a)
    if any(a.lower() == n.lower() for names in GENRES.values() for n in names):
        return a
    return re.split(r",| & | X | feat\.? ", a, maxsplit=1)[0].strip() or "Unknown"


def genre_of(artist):
    al = artist.lower()
    for g, names in GENRES.items():  # exact name first
        if any(al == n.lower() for n in names):
            return g
    for g, names in GENRES.items():  # then "Name feat..." / "Name, Other" style prefixes (whole words only)
        if any(re.match(re.escape(n.lower()) + r"(\W|$)", al) for n in names):
            return g
    return "Other"


# guitar players / cover channels: their videos are filed under the performer, not the original artist
PERFORMERS = ["Danny Yau", "Ichika Nito", "Zenith Akirae", "TACT.mp3", "Sungha Jung", "Tim Henson", "Manuel Gardner-Fernandes"]
COVER_WORDS = re.compile(r"cover|flex|session|on 6-string|full cover|guitar|mashup", re.I)
CHANNELS = {}
_cf = os.path.join(os.path.dirname(os.path.abspath(__file__)), "channels.json")
if os.path.exists(_cf):
    CHANNELS = {os.path.basename(k): v for k, v in json.load(open(_cf, encoding="utf-8")).items()}


def performer(fname, tag_artist, title):
    ch = CHANNELS.get(os.path.basename(fname), "")
    if ch and any(ch.lower().startswith(p.lower()) for p in PERFORMERS) and ch.lower().replace(" ", "") not in tag_artist.lower().replace(" ", ""):
        return ch.replace("Manuel Gardner-Fernandes", "Manuel Gardner Fernandes").replace("TACT.mp3", "TACT"), True
    if ch and COVER_WORDS.search(title + " " + fname) and ch.lower() not in ("hybe labels", "smtown", "jyp entertainment", "starship", "bigbang", "officialgdragon"):
        if any(ch.lower().startswith(p.lower()) for p in PERFORMERS):
            return ch, True
    return tag_artist, False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=os.path.expanduser("~/Music"))
    ap.add_argument("--sd", default=os.path.expanduser("~/Music/WaveOS_SD"))
    args = ap.parse_args()
    out = args.sd
    os.makedirs(os.path.join(out, "music"), exist_ok=True)
    os.makedirs(os.path.join(out, "playlists"), exist_ok=True)

    seen = {}          # (artist, title) -> sd path
    seen_id = {}       # youtube video id -> sd path (exact same song)
    playlists = {}     # name -> [sd paths]
    rows = []
    skipped = 0
    dirs = sorted(glob.glob(os.path.join(args.src, "YouTube_Playlist_*"))) + [d for d in [os.path.join(args.src, "mus for waveos")] if os.path.isdir(d)]
    for d in dirs:
        plname = os.path.basename(d).replace("YouTube_Playlist_", "")
        plname = {"Shuffled": "Shuffled Mix", "Rock": "Rock Classics", "mus for waveos": "New Additions"}.get(plname, plname)
        for f in sorted(glob.glob(os.path.join(d, "*.mp3"))):
            try:
                tags = ID3(f)
            except Exception:
                tags = ID3()
            vid = (re.search(r"v=([\w-]{11})", str(tags.get("TXXX:purl", ""))) or [None, None])[1]
            if vid and vid in seen_id:  # exact same video already in the library
                playlists.setdefault(plname, []).append(seen_id[vid])
                skipped += 1
                continue
            raw_title = str(tags.get("TIT2", "")) or re.sub(r"^\d+ - ", "", os.path.splitext(os.path.basename(f))[0])
            raw_artist = fix_artist(str(tags.get("TPE1", "")) or "Unknown", raw_title)
            title = clean_title(raw_title, main_artist(raw_artist))
            if not CHANNELS.get(os.path.basename(f)):  # new files: tag artist is the performer
                title = cover_title(title, main_artist(raw_artist))
            perf, is_cover = performer(f, raw_artist, title)
            if is_cover:
                orig = main_artist(raw_artist)
                if orig.lower() not in title.lower() and orig.lower() != perf.lower():
                    title = f"{title} ({orig} cover)" if "cover" not in title.lower() else f"{title} - {orig}"
                raw_artist = perf
            artist = main_artist(raw_artist)
            if raw_artist != artist and "," in raw_artist or " & " in raw_artist:
                title_full = title  # keep collab info in the artist tag only
            key = (artist.lower(), re.sub(r"\W", "", title.lower()))
            genre = genre_of(artist)
            if key in seen:
                skipped += 1
            if key not in seen:
                rel = f"/music/{safe(genre)}/{safe(artist)}/{safe(title)}.mp3"
                dst = out + rel
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                if not os.path.exists(dst) or os.path.getsize(dst) != os.path.getsize(f):
                    shutil.copyfile(f, dst)
                    t = ID3(dst)
                    for k in list(t.keys()):  # drop big description frames, keep cover art
                        if k.startswith(("TXXX", "COMM", "TSSE")):
                            del t[k]
                    t.setall("TIT2", [TIT2(encoding=3, text=title)])
                    t.setall("TPE1", [TPE1(encoding=3, text=ascii_punct(raw_artist))])
                    t.setall("TALB", [TALB(encoding=3, text=genre)])
                    t.setall("TCON", [TCON(encoding=3, text=genre)])
                    t.save(dst, v2_version=3)
                seen[key] = rel
                rows.append((genre, artist, title, rel, raw_artist))
            if vid:
                seen_id[vid] = seen[key]
            playlists.setdefault(plname, []).append(seen[key])

    # the full YouTube playlist in its original order (from tools/playlist_order.txt: index|id|title|channel)
    order_file = os.path.join(os.path.dirname(os.path.abspath(__file__)), "playlist_order.txt")
    if os.path.exists(order_file):
        full = []
        for line in open(order_file, encoding="utf-8"):
            parts = line.rstrip("\n").split("|")
            if len(parts) >= 2 and parts[1] in seen_id and seen_id[parts[1]] not in full:
                full.append(seen_id[parts[1]])
        playlists["My YouTube Playlist"] = full
    for g in sorted({r[0] for r in rows}):
        playlists[g] = [r[3] for r in sorted(rows, key=lambda r: (r[1].lower(), r[2].lower())) if r[0] == g]
    for name, kws in SMART.items():
        playlists[name] = [r[3] for r in rows if any(k in r[4].lower() for k in kws)]

    for name, paths in playlists.items():
        if not paths:
            continue
        with open(os.path.join(out, "playlists", safe(name) + ".m3u"), "w", encoding="utf-8") as fh:
            fh.write("#EXTM3U\n")
            for p in paths:
                fh.write(p + "\n")

    print(f"{len(rows)} unique songs -> {out}   ({skipped} duplicates skipped)")
    for g in sorted({r[0] for r in rows}):
        print(f"  {g:7} {sum(1 for r in rows if r[0] == g)} songs, {len({r[1] for r in rows if r[0] == g})} artists")
    print("playlists:", ", ".join(f"{k} ({len(v)})" for k, v in playlists.items() if v))


if __name__ == "__main__":
    main()
