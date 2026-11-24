// WAVE OS - DAP: WAV/MP3/FLAC/AAC, library (songs, folders, favourites, playlists, queue,
// most played, recent), ID3 tags, pixel-art album art, synced lyrics, EQ, speed, A-B,
// sleep timer, bookmarks, resume, stats, shake-to-shuffle, visualisers incl. big "car mode".
#include "common.h"
#include "audio.h"
#include <algorithm>

// ===================================================================== library
// Compact path store: all song paths packed into 4KB blocks (no per-song heap chunk, no big
// contiguous buffer) + a 16-bit sort order. ~45% of the RAM a vector<String> needs.
struct PathPool {
  static constexpr int BS = 4096;
  std::vector<char*> blocks;
  std::vector<uint32_t> off;   // (block << 12) | pos
  std::vector<uint16_t> ord;   // display/sort order
  size_t used = BS;
  void clear() {
    for (auto b : blocks) free(b);
    blocks.clear(); blocks.shrink_to_fit(); off.clear(); off.shrink_to_fit(); ord.clear(); ord.shrink_to_fit();
    used = BS;
  }
  bool push_back(const String& s) {
    size_t n = s.length() + 1;
    if (n > 256 || off.size() >= 4000) return false;
    if (used + n > BS) { char* b = (char*)malloc(BS); if (!b) return false; blocks.push_back(b); used = 0; }
    memcpy(blocks.back() + used, s.c_str(), n);
    off.push_back(((uint32_t)(blocks.size() - 1) << 12) | used);
    ord.push_back(off.size() - 1);
    used += n;
    return true;
  }
  const char* c(int i) const { uint32_t o = off[ord[i]]; return blocks[o >> 12] + (o & 4095); }
  String operator[](int i) const { return String(c(i)); }
  size_t size() const { return ord.size(); }
  bool empty() const { return ord.empty(); }
  void sortByName() {
    auto base = [this](uint16_t k) { uint32_t o = off[k]; const char* p = blocks[o >> 12] + (o & 4095); const char* b = strrchr(p, '/'); return b ? b + 1 : p; };
    std::sort(ord.begin(), ord.end(), [&](uint16_t a, uint16_t b) { return strcasecmp(base(a), base(b)) < 0; });
  }
};

namespace Lib {
PathPool files;
std::vector<String> favs;
bool scanned = false;

static bool isAudio(const String& n) {
  String l = n; l.toLowerCase();
  return l.endsWith(".wav") || l.endsWith(".mp3") || l.endsWith(".flac") || l.endsWith(".aac");
}
static void scanDir(const String& path, int depth) {
  File d = SD.open(path);
  if (!d || !d.isDirectory()) return;
  while (files.size() < 1500) {
    File f = d.openNextFile();
    if (!f) break;
    String n = f.name();
    String full = path == "/" ? "/" + n : path + "/" + n;
    bool dir = f.isDirectory();
    f.close();
    if (n.startsWith(".")) continue;
    if (dir) { if (depth > 0 && n != "System Volume Information") scanDir(full, depth - 1); }
    else if (isAudio(n)) files.push_back(full);
  }
  d.close();
}
void scan() {
  files.clear();
  if (!sdOK) { scanned = true; return; }
  scanDir("/music", 5);
  if (files.empty()) scanDir("/", 1);
  files.sortByName();
  scanned = true;
}
int find(const String& p) {
  for (int i = 0; i < (int)files.size(); i++) if (!strcmp(files.c(i), p.c_str())) return i;
  return -1;
}
void loadFavs() {
  favs.clear();
  File f = SD.open("/waveos/favs.txt");
  if (!f) return;
  while (f.available()) { String l = f.readStringUntil('\n'); l.trim(); if (l.length()) favs.push_back(l); }
  f.close();
}
void saveFavs() {
  File f = SD.open("/waveos/favs.txt", FILE_WRITE);
  if (!f) return;
  for (auto& s : favs) f.println(s);
  f.close();
}
bool isFav(const String& p) { return std::find(favs.begin(), favs.end(), p) != favs.end(); }

// folders = unique parent directories
std::vector<String> folders() {
  std::vector<String> out;
  for (int i = 0; i < (int)files.size(); i++) {
    String f = files[i];
    String d = f.substring(0, f.lastIndexOf('/'));
    if (std::find(out.begin(), out.end(), d) == out.end()) out.push_back(d);
  }
  std::sort(out.begin(), out.end());
  return out;
}
std::vector<int> inFolder(const String& d) {
  std::vector<int> v;
  for (int i = 0; i < (int)files.size(); i++) if (files[i].substring(0, files[i].lastIndexOf('/')) == d) v.push_back(i);
  return v;
}
// playlists (.m3u in /playlists)
std::vector<String> playlists() {
  std::vector<String> v;
  File d = SD.open("/playlists");
  if (!d) return v;
  while (true) {
    File f = d.openNextFile();
    if (!f) break;
    String n = f.name();
    String l = n; l.toLowerCase();
    if (!f.isDirectory() && (l.endsWith(".m3u") || l.endsWith(".m3u8"))) v.push_back(n);
    f.close();
  }
  d.close();
  std::sort(v.begin(), v.end());
  return v;
}
std::vector<int> loadM3U(const String& name) {
  std::vector<int> v;
  File f = SD.open("/playlists/" + name);
  if (!f) return v;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (!l.length() || l.startsWith("#")) continue;
    l.replace("\\", "/");
    int idx = find(l.startsWith("/") ? l : "/playlists/" + l);
    if (idx < 0) {  // match by file name
      String fn = l.substring(l.lastIndexOf('/') + 1);
      for (int i = 0; i < (int)files.size(); i++) if (files[i].endsWith("/" + fn)) { idx = i; break; }
    }
    if (idx >= 0) v.push_back(idx);
  }
  f.close();
  return v;
}
void appendM3U(const String& name, const String& path) {
  ensureDir("/playlists");
  bool exists = SD.exists("/playlists/" + name);
  File f = SD.open("/playlists/" + name, exists ? FILE_APPEND : FILE_WRITE);
  if (!f) return;
  if (!exists) f.println("#EXTM3U");
  f.println(path);
  f.close();
}
}  // namespace Lib

// ===================================================================== stats + bookmarks
namespace Stats {
struct S { String path; uint16_t count; uint32_t seq; };
std::vector<S> st;
uint32_t seqCtr = 0, totalSec = 0;
bool dirty = false;
uint32_t dirtyT = 0;
struct BM { String path; uint32_t pos; };
std::vector<BM> bms;
void load() {
  st.clear(); bms.clear();
  File f = SD.open("/waveos/stats.txt");
  if (f) {
    while (f.available()) {
      String l = f.readStringUntil('\n');
      if (l.startsWith("#total ")) { totalSec = l.substring(7).toInt(); continue; }
      int a = l.indexOf('\t'), b = l.indexOf('\t', a + 1);
      if (a < 0 || b < 0) continue;
      S s{l.substring(b + 1), (uint16_t)l.substring(0, a).toInt(), (uint32_t)l.substring(a + 1, b).toInt()};
      s.path.trim();
      seqCtr = std::max(seqCtr, s.seq);
      st.push_back(s);
    }
    f.close();
  }
  f = SD.open("/waveos/bookmarks.txt");
  if (f) {
    while (f.available()) {
      String l = f.readStringUntil('\n');
      int a = l.indexOf('\t');
      if (a < 0) continue;
      String p = l.substring(a + 1); p.trim();
      bms.push_back({p, (uint32_t)l.substring(0, a).toInt()});
    }
    f.close();
  }
}
void save() {
  if (!sdOK) return;
  File f = SD.open("/waveos/stats.txt", FILE_WRITE);
  if (f) {
    f.printf("#total %u\n", totalSec);
    for (auto& s : st) f.printf("%u\t%u\t%s\n", s.count, s.seq, s.path.c_str());
    f.close();
  }
  f = SD.open("/waveos/bookmarks.txt", FILE_WRITE);
  if (f) { for (auto& b : bms) f.printf("%u\t%s\n", b.pos, b.path.c_str()); f.close(); }
}
void played(const String& p) {  // saved a bit later (not during the song change)
  dirty = true; dirtyT = millis();
  for (auto& s : st) if (s.path == p) { s.count++; s.seq = ++seqCtr; return; }
  st.push_back({p, 1, ++seqCtr});
  if (st.size() > 150) {  // keep memory bounded: drop the least played
    auto it = std::min_element(st.begin(), st.end(), [](const S& a, const S& b) { return a.count != b.count ? a.count < b.count : a.seq < b.seq; });
    st.erase(it);
  }
}
std::vector<int> mostPlayed() {
  std::vector<S> c = st;
  std::sort(c.begin(), c.end(), [](const S& a, const S& b) { return a.count > b.count; });
  std::vector<int> v;
  for (auto& s : c) { int i = Lib::find(s.path); if (i >= 0) v.push_back(i); if (v.size() >= 50) break; }
  return v;
}
std::vector<int> recent() {
  std::vector<S> c = st;
  std::sort(c.begin(), c.end(), [](const S& a, const S& b) { return a.seq > b.seq; });
  std::vector<int> v;
  for (auto& s : c) { int i = Lib::find(s.path); if (i >= 0) v.push_back(i); if (v.size() >= 50) break; }
  return v;
}
int playCount(const String& p) { for (auto& s : st) if (s.path == p) return s.count; return 0; }
uint32_t bookmark(const String& p) { for (auto& b : bms) if (b.path == p) return b.pos; return 0; }
void setBookmark(const String& p, uint32_t pos) {
  for (auto& b : bms) if (b.path == p) { b.pos = pos; return; }
  bms.push_back({p, pos});
  if (bms.size() > 40) bms.erase(bms.begin());
}
}  // namespace Stats

// ===================================================================== metadata, album art, lyrics
namespace Meta {
String title, artist, album;
bool hasArt = false;
struct Lyr { float t; String s; };
std::vector<Lyr> lyrics;

static String clean(const uint8_t* p, int n, int enc) {
  String s;
  if (enc == 1 || enc == 2) {  // UTF-16: keep ASCII
    int i = 0;
    bool be = enc == 2;
    if (n >= 2 && ((p[0] == 0xFF && p[1] == 0xFE) || (p[0] == 0xFE && p[1] == 0xFF))) { be = p[0] == 0xFE; i = 2; }
    for (; i + 1 < n; i += 2) {
      uint16_t c = be ? (p[i] << 8 | p[i + 1]) : (p[i] | p[i + 1] << 8);
      if (!c) break;
      s += (c >= 32 && c < 127) ? (char)c : '?';
    }
  } else {
    for (int i = 0; i < n && p[i]; i++) {
      if (p[i] >= 32 && p[i] < 127) s += (char)p[i];
      else if (p[i] >= 0xC0) s += '?';  // utf-8 lead byte (continuations skipped)
    }
  }
  s.trim();
  return s;
}

static void readID3(const String& path) {
  File f = SD.open(path);
  if (!f) return;
  uint8_t h[10];
  if (f.read(h, 10) != 10 || memcmp(h, "ID3", 3)) { f.close(); return; }
  int ver = h[3];
  uint32_t size = (h[6] & 0x7F) << 21 | (h[7] & 0x7F) << 14 | (h[8] & 0x7F) << 7 | (h[9] & 0x7F);
  uint32_t pos = 10;
  while (pos + 10 < size + 10) {
    uint8_t fh[10];
    f.seek(pos);
    if (f.read(fh, ver == 2 ? 6 : 10) < 6) break;
    if (!fh[0]) break;
    char id[5] = {0};
    uint32_t fs;
    int hl;
    if (ver == 2) { memcpy(id, fh, 3); fs = fh[3] << 16 | fh[4] << 8 | fh[5]; hl = 6; }
    else {
      memcpy(id, fh, 4);
      fs = ver == 4 ? ((fh[4] & 0x7F) << 21 | (fh[5] & 0x7F) << 14 | (fh[6] & 0x7F) << 7 | (fh[7] & 0x7F))
                    : ((uint32_t)fh[4] << 24 | fh[5] << 16 | fh[6] << 8 | fh[7]);
      hl = 10;
    }
    if (fs == 0 || fs > size) break;
    bool isT = !strcmp(id, "TIT2") || !strcmp(id, "TT2"), isA = !strcmp(id, "TPE1") || !strcmp(id, "TP1"), isB = !strcmp(id, "TALB") || !strcmp(id, "TAL");
    if ((isT || isA || isB) && fs < 256) {
      uint8_t b[256];
      f.read(b, fs);
      String s = clean(b + 1, fs - 1, b[0]);
      if (isT) title = s; else if (isA) artist = s; else album = s;
    }
    pos += hl + fs;
  }
  f.close();
}
static void loadLyrics(const String& path) {
  lyrics.clear();
  String lp = path.substring(0, path.lastIndexOf('.')) + ".lrc";
  File f = SD.open(lp);
  if (!f) return;
  while (f.available() && lyrics.size() < 300) {
    String l = f.readStringUntil('\n'); l.trim();
    std::vector<float> ts;
    while (l.startsWith("[")) {
      int e = l.indexOf(']');
      if (e < 0) break;
      String tag = l.substring(1, e);
      int c = tag.indexOf(':');
      if (c > 0 && isdigit((unsigned char)tag[0])) ts.push_back(tag.substring(0, c).toInt() * 60 + tag.substring(c + 1).toFloat());
      l = l.substring(e + 1);
    }
    for (float t : ts) lyrics.push_back({t, clean((const uint8_t*)l.c_str(), l.length(), 3)});
  }
  f.close();
  std::sort(lyrics.begin(), lyrics.end(), [](const Lyr& a, const Lyr& b) { return a.t < b.t; });
}
void load(const String& path) {
  title = baseName(path); artist = parentName(path); album = "";
  hasArt = false;
  String l = path; l.toLowerCase();
  if (l.endsWith(".mp3") || l.endsWith(".aac")) readID3(path);
  if (!title.length()) title = baseName(path);
  loadLyrics(path);
}
int lyricIdx(float t) {
  int k = -1;
  for (int i = 0; i < (int)lyrics.size(); i++) if (lyrics[i].t <= t) k = i; else break;
  return k;
}
}  // namespace Meta

// ===================================================================== controller (runs in background)
namespace Ctl {
int cur = -1;
std::vector<int> source;  // what we are playing from (all songs, a folder, a playlist...)
String sourceName = "ALL SONGS";
std::vector<int> order;
std::vector<int> queue;
int orderPos = 0;
bool shuffle = false;
int repeat = 1;  // 0 off, 1 all, 2 one
int eq = 0, vis = 0;
bool lyricsView = false;
int sleepMin = 0;
uint32_t sleepAt = 0, lastSave = 0, lastSec = 0;
float speed = 1.0f;
struct Preset { const char* n; float b, t; };
const Preset EQ[] = {{"FLAT", 0, 0}, {"BASS", 8, 0}, {"ROCK", 5, 4}, {"VOCAL", -3, 4}, {"METAL", 6, 6}, {"TREBLE", 0, 7}, {"LO-FI", -2, -10}, {"LOUD", 5, 3}};
constexpr int NEQ = sizeof(EQ) / sizeof(EQ[0]);
const char* RPT[] = {"OFF", "ALL", "ONE"};

void buildOrder() {
  order = source;
  if (order.empty()) for (int i = 0; i < (int)Lib::files.size(); i++) order.push_back(i);
  if (shuffle) {
    for (int i = order.size() - 1; i > 0; i--) std::swap(order[i], order[random(i + 1)]);
    auto it = std::find(order.begin(), order.end(), cur);
    if (it != order.end()) std::swap(*it, order[0]);
  }
  orderPos = 0;
  for (int i = 0; i < (int)order.size(); i++) if (order[i] == cur) orderPos = i;
}
void setSource(const std::vector<int>& s, const char* name) { source = s; sourceName = name; buildOrder(); }
void saveState() {
  if (!sdOK || cur < 0) return;
  File f = SD.open("/waveos/music.txt", FILE_WRITE);
  if (!f) return;
  f.printf("%s\n%d\n%d\n%d\n%d\n%d\n", Lib::files[cur].c_str(), (int)Music::pos(), shuffle, repeat, eq, vis);
  f.close();
}
void rememberBookmark() {
  if (cur >= 0 && Music::active() && Music::len() > 600) {
    float p = Music::pos();
    Stats::setBookmark(Lib::files[cur], p < Music::len() - 30 ? (uint32_t)p : 0);
  }
}
bool play(int idx, float start = -1) {
  if (idx < 0 || idx >= (int)Lib::files.size()) return false;
  rememberBookmark();
  cur = idx;
  const String& p = Lib::files[idx];
  if (start < 0) start = Stats::bookmark(p);  // long files continue where you stopped
  if (!Music::open(p.c_str(), start)) { toast(Music::error(), 3000); memLog("playfail"); return false; }
  Music::setSpeed(speed);
  Music::setEQ(EQ[eq].b, EQ[eq].t);
  Music::setGain(1);
  Meta::load(p);
  for (int i = 0; i < (int)order.size(); i++) if (order[i] == idx) orderPos = i;
  Stats::played(p);
  saveState();
  return true;
}
void next(bool user) {
  if (!queue.empty()) { int q = queue.front(); queue.erase(queue.begin()); play(q, 0); return; }
  if (order.empty()) return;
  if (!user && repeat == 2) { play(cur, 0); return; }
  int p = orderPos + 1;
  if (p >= (int)order.size()) {
    if (!user && repeat == 0) { Music::pause(true); return; }
    p = 0;
  }
  for (int tries = 0; tries < 5; tries++, p = (p + 1) % order.size())
    if (play(order[p], 0)) return;
}
void prev() {
  if (order.empty()) return;
  if (Music::pos() > 3) { Music::seek(0); return; }
  play(order[(orderPos + order.size() - 1) % order.size()], 0);
}
void loadState() {
  File f = SD.open("/waveos/music.txt");
  if (!f) return;
  String path = f.readStringUntil('\n'); path.trim();
  int pos = f.readStringUntil('\n').toInt();
  shuffle = f.readStringUntil('\n').toInt();
  repeat = constrain((int)f.readStringUntil('\n').toInt(), 0, 2);
  eq = constrain((int)f.readStringUntil('\n').toInt(), 0, NEQ - 1);
  vis = constrain((int)f.readStringUntil('\n').toInt(), 0, 3);
  f.close();
  int i = Lib::find(path);
  if (i >= 0) {
    cur = i;
    buildOrder();
    if (play(i, pos)) Music::pause(true);  // ready to resume
  }
}
}  // namespace Ctl

void libEnsureScanned() {
  if (Lib::scanned) return;
  Lib::scan(); Lib::loadFavs(); Stats::load();
  memLog("scan");
  Ctl::setSource({}, "ALL SONGS");
  Ctl::loadState();
}
void searchSongs(const String& q, std::vector<int>& out, int max) {
  libEnsureScanned();
  String lq = q; lq.toLowerCase();
  for (int i = 0; i < (int)Lib::files.size() && (int)out.size() < max; i++) {
    String n = baseName(Lib::files[i]); n.toLowerCase();
    if (n.indexOf(lq) >= 0) out.push_back(i);
  }
}
String songPath(int idx) { return idx >= 0 && idx < (int)Lib::files.size() ? Lib::files[idx] : String(); }
void playSong(int idx) { libEnsureScanned(); Ctl::play(idx); openApp(appMusic); }

static uint32_t shakeT = 0;
static int shakeN = 0;
void musicTick() {
  if (Music::takeEnded()) Ctl::next(false);
  if (Stats::dirty && millis() - Stats::dirtyT > 20000) { Stats::dirty = false; Stats::save(); }
  Music::abTick();
  if (Ctl::sleepAt) {
    int32_t left = (int32_t)(Ctl::sleepAt - millis());
    if (left <= 0) { Music::pause(true); Music::setGain(1); Ctl::sleepAt = 0; Ctl::sleepMin = 0; toast("SLEEP TIMER: GOOD NIGHT"); }
    else if (left < 8000) Music::setGain(left / 8000.f);
  }
  bool playing = Music::active() && !Music::paused();
  if (playing && millis() - Ctl::lastSec >= 1000) { Ctl::lastSec = millis(); Stats::totalSec++; }
  if (playing && millis() - Ctl::lastSave > 15000) {
    Ctl::lastSave = millis(); Ctl::rememberBookmark(); Ctl::saveState();
    if (Music::len() > 600) Stats::save();
  }
  // shake to shuffle
  float ax, ay, az;
  static uint32_t imuT = 0;
  bool imuNow = millis() - imuT > 80;
  if (imuNow) imuT = millis();
  if (cfg.shake && playing && imuNow && imuAccel(ax, ay, az)) {
    float g = sqrtf(ax * ax + ay * ay + az * az);
    if (g > 2.3f) {
      if (millis() - shakeT < 600) shakeN++; else shakeN = 1;
      shakeT = millis();
      if (shakeN >= 2 && !Lib::files.empty()) { shakeN = -10; Ctl::play(random(Lib::files.size()), 0); toast("SHAKE! RANDOM SONG"); }
    }
  }
}

// ===================================================================== UI
struct MusicApp : App {
  enum { V_PLAYING, V_SONGS, V_FOLDERS, V_FAVS, V_LISTS, V_QUEUE, V_COUNT, V_CTX };
  enum { R_SONG, R_FOLDER, R_LIST, R_HEADER, R_SHUFFLE };
  struct Row { uint8_t type; int16_t idx, aux; };  // tiny: names are looked up only when drawn
  std::vector<String> hdrs;                         // section header texts
  const char* SHUF[3] = {"SHUFFLE ALL SONGS", "SHUFFLE FAVOURITES", "SHUFFLE THIS LIST"};
  // path segments without allocating: /music/<Genre>/<Artist>/<Title>.ext
  static void seg(const String& p, int which, const char*& s, int& n) {  // 0 title, 1 artist, 2 genre
    const char* c = p.c_str();
    int L = p.length(), last = p.lastIndexOf('/');
    if (which == 0) { int dot = p.lastIndexOf('.'); s = c + last + 1; n = (dot > last ? dot : L) - last - 1; return; }
    int prev = last > 0 ? p.lastIndexOf('/', last - 1) : -1;
    if (which == 1) { s = c + prev + 1; n = last - prev - 1; return; }
    if (p.startsWith("/music/")) { int e = p.indexOf('/', 7); if (e > 0 && e < last) { s = c + 7; n = e - 7; return; } }
    s = "OTHER"; n = 5;
  }
  static int cmpSeg(const String& a, const String& b, int which) {
    const char *x, *y; int nx, ny;
    seg(a, which, x, nx); seg(b, which, y, ny);
    for (int i = 0; i < nx && i < ny; i++) { int d = toupper((unsigned char)x[i]) - toupper((unsigned char)y[i]); if (d) return d; }
    return nx - ny;
  }
  static String segStr(const String& p, int which) { const char* s; int n; seg(p, which, s, n); String r; r.reserve(n); for (int i = 0; i < n; i++) r += s[i]; return r; }
  String rowLabel(const Row& r) {
    switch (r.type) {
      case R_SONG: return segStr(Lib::files[r.idx], 0);
      case R_FOLDER: return parentName(folderCache[r.idx] + "/x");
      case R_LIST: return r.idx == 0 ? String("MOST PLAYED") : r.idx == 1 ? String("RECENTLY PLAYED") : baseName(plCache[r.idx - 2]);
      case R_HEADER: return hdrs[r.aux];
      default: return String(SHUF[r.aux]);
    }
  }
  String rowSub(const Row& r) {
    if (r.type == R_SONG) return segStr(Lib::files[r.idx], 1);
    if (r.type == R_FOLDER || r.type == R_SHUFFLE) return String(r.type == R_FOLDER ? r.aux : r.idx);
    return String();
  }
  std::vector<Row> rows;
  int view = V_PLAYING;
  int sel = 0, top = 0;
  int sortMode = 0;  // songs: 0 title, 1 artist, 2 genre, 3 most played
  bool help = false;
  float bars[16] = {0}, peaks[16] = {0};
  float vu = 0;
  uint32_t enterT = 0;
  std::vector<int> ctx;
  String ctxName;
  int ctxFrom = V_FOLDERS;
  std::vector<String> folderCache, plCache;
  const char* SORTN[4] = {"TITLE", "ARTIST", "GENRE", "MOST PLAYED"};
  MusicApp() { name = "MUSIC"; }

  // /music/<Genre>/<Artist>/<Title>.mp3 -> genre / artist / title
  static String genreOf(const String& p) {
    if (!p.startsWith("/music/")) return "OTHER";
    int a = 7, b = p.indexOf('/', a);
    if (b < 0) return "OTHER";
    int c = p.indexOf('/', b + 1);
    return c < 0 ? String("OTHER") : p.substring(a, b);
  }
  static String artistOf(const String& p) { return parentName(p); }
  static String up(String s) { s.toUpperCase(); return s; }

  const char* viewName(int v) {
    static const char* n[] = {"NOW PLAYING", "SONGS", "FOLDERS", "FAVOURITES", "PLAYLISTS", "QUEUE"};
    return v == V_CTX ? "" : n[v];
  }
  std::vector<int> favItems() {
    std::vector<int> v;
    for (auto& p : Lib::favs) { int i = Lib::find(p); if (i >= 0) v.push_back(i); }
    return v;
  }
  void addSongs(std::vector<int> v, bool sorted, int shuffleLabel) {
    if (v.size() > 1 && shuffleLabel >= 0) rows.push_back({R_SHUFFLE, (int16_t)v.size(), (int16_t)shuffleLabel});
    if (sorted) {
      const auto& F = Lib::files;
      if (sortMode == 3) std::stable_sort(v.begin(), v.end(), [](int x, int y) { return Stats::playCount(Lib::files[x]) > Stats::playCount(Lib::files[y]); });
      else if (sortMode == 0) std::sort(v.begin(), v.end(), [&](int x, int y) { return cmpSeg(F[x], F[y], 0) < 0; });
      else if (sortMode == 1) std::sort(v.begin(), v.end(), [&](int x, int y) { int d = cmpSeg(F[x], F[y], 1); return d ? d < 0 : cmpSeg(F[x], F[y], 0) < 0; });
      else std::sort(v.begin(), v.end(), [&](int x, int y) {
        int d = cmpSeg(F[x], F[y], 2); if (d) return d < 0;
        d = cmpSeg(F[x], F[y], 1); return d ? d < 0 : cmpSeg(F[x], F[y], 0) < 0; });
    }
    rows.reserve(rows.size() + v.size() + 40);
    int prev = -1;
    for (int i : v) {
      if (sorted && sortMode != 3) {
        bool newSec;
        if (prev < 0) newSec = true;
        else if (sortMode == 0) { const char *a, *b; int na, nb; seg(Lib::files[i], 0, a, na); seg(Lib::files[prev], 0, b, nb); newSec = toupper((unsigned char)a[0]) != toupper((unsigned char)b[0]) && (isalpha((unsigned char)a[0]) || isalpha((unsigned char)b[0])); }
        else newSec = cmpSeg(Lib::files[i], Lib::files[prev], sortMode == 1 ? 1 : 2) != 0;
        if (newSec) {
          String h = segStr(Lib::files[i], sortMode == 0 ? 0 : sortMode == 1 ? 1 : 2);
          h.toUpperCase();
          if (sortMode == 0) h = h.length() && isalpha((unsigned char)h[0]) ? h.substring(0, 1) : String("#");
          hdrs.push_back(h);
          rows.push_back({R_HEADER, -1, (int16_t)(hdrs.size() - 1)});
        }
      }
      rows.push_back({R_SONG, (int16_t)i, 0});
      prev = i;
    }
  }
  std::vector<int> songsOfRows() {
    std::vector<int> v;
    for (auto& r : rows) if (r.type == R_SONG) v.push_back(r.idx);
    return v;
  }
  void build() {
    rows.clear(); rows.shrink_to_fit();
    hdrs.clear();
    switch (view) {
      case V_SONGS: {
        std::vector<int> all;
        all.reserve(Lib::files.size());
        for (int i = 0; i < (int)Lib::files.size(); i++) all.push_back(i);
        addSongs(all, true, 0);
        break;
      }
      case V_FOLDERS: {
        folderCache = Lib::folders();
        std::vector<std::pair<String, String>> f;  // (genre, folder)
        for (auto& d : folderCache) f.push_back({up(genreOf(d + "/x")), d});
        std::sort(f.begin(), f.end(), [](const std::pair<String, String>& x, const std::pair<String, String>& y) {
          return x.first != y.first ? x.first < y.first : up(parentName(x.second + "/x")) < up(parentName(y.second + "/x")); });
        String lastG;
        for (size_t i = 0; i < f.size(); i++) {
          if (f[i].first != lastG) { hdrs.push_back(f[i].first); rows.push_back({R_HEADER, -1, (int16_t)(hdrs.size() - 1)}); lastG = f[i].first; }
          int idx = std::find(folderCache.begin(), folderCache.end(), f[i].second) - folderCache.begin();
          int cnt = 0, L = f[i].second.length();
          for (int q = 0; q < (int)Lib::files.size(); q++) {
            const char* p = Lib::files.c(q);
            if ((int)strlen(p) > L && p[L] == '/' && !strncmp(p, f[i].second.c_str(), L) && !strchr(p + L + 1, '/')) cnt++;
          }
          rows.push_back({R_FOLDER, (int16_t)idx, (int16_t)cnt});
        }
        break;
      }
      case V_FAVS: addSongs(favItems(), false, 1); break;
      case V_LISTS:
        plCache = Lib::playlists();
        hdrs.push_back("SMART"); rows.push_back({R_HEADER, -1, 0});
        rows.push_back({R_LIST, 0, 0});
        rows.push_back({R_LIST, 1, 0});
        if (!plCache.empty()) { hdrs.push_back("PLAYLISTS"); rows.push_back({R_HEADER, -1, 1}); }
        for (int i = 0; i < (int)plCache.size(); i++) rows.push_back({R_LIST, (int16_t)(i + 2), 0});
        break;
      case V_QUEUE: addSongs(Ctl::queue, false, -1); break;
      case V_CTX: addSongs(ctx, false, 2); break;
    }
    sel = constrain(sel, 0, std::max(0, (int)rows.size() - 1));
    skipHeader(1);
  }
  void skipHeader(int dir) {
    int n = rows.size();
    for (int k = 0; k < n && rows[sel].type == R_HEADER; k++) sel = (sel + dir + n) % n;
  }
  void setView(int v) {
    view = v; sel = 0; top = 0;
    build();
    if (v == V_SONGS && Ctl::cur >= 0)
      for (int i = 0; i < (int)rows.size(); i++) if (rows[i].type == R_SONG && rows[i].idx == Ctl::cur) { sel = i; break; }
  }
  void openCtx(const std::vector<int>& v, const String& nm, int from) { ctx = v; ctxName = nm; ctxFrom = from; view = V_CTX; sel = 0; top = 0; build(); }
  void enter() override {
    enterT = millis();
    libEnsureScanned();
    if (view == V_CTX) { build(); return; }
    setView((Music::active() || Lib::files.empty()) ? V_PLAYING : V_SONGS);
  }
  void leave() override { Ctl::rememberBookmark(); Stats::save(); }
  void playFrom(int song, bool shuffle) {
    std::vector<int> src = songsOfRows();
    const char* nm = view == V_SONGS ? "ALL SONGS" : view == V_FAVS ? "FAVOURITES" : view == V_QUEUE ? "QUEUE" : ctxName.c_str();
    if (shuffle) Ctl::shuffle = true;
    Ctl::cur = shuffle ? src[random(src.size())] : song;
    Ctl::setSource(src, nm);
    if (Ctl::play(Ctl::cur)) {
      if (shuffle) toast("SHUFFLE ON", 900);
      setView(V_PLAYING);
    }
  }

  bool listKey(const KeyEv& e) {
    KeyCode k = e.nav();
    int n = rows.size();
    if (k == K_UP && n) { sel = (sel + n - 1) % n; skipHeader(-1); return true; }
    if (k == K_DOWN && n) { sel = (sel + 1) % n; skipHeader(1); return true; }
    if (k == K_LEFT && n) { sel = std::max(0, sel - 8); skipHeader(1); return true; }
    if (k == K_RIGHT && n) { sel = std::min(n - 1, sel + 8); skipHeader(-1); return true; }
    if (e.code == K_ESC && view == V_CTX) { setView(ctxFrom); return true; }
    if (k == K_ENTER && n) {
      Row& r = rows[sel];
      if (r.type == R_FOLDER) { openCtx(Lib::inFolder(folderCache[r.idx]), rowLabel(r), V_FOLDERS); return true; }
      if (r.type == R_LIST) {
        if (r.idx == 0) openCtx(Stats::mostPlayed(), "MOST PLAYED", V_LISTS);
        else if (r.idx == 1) openCtx(Stats::recent(), "RECENTLY PLAYED", V_LISTS);
        else openCtx(Lib::loadM3U(plCache[r.idx - 2]), baseName(plCache[r.idx - 2]), V_LISTS);
        return true;
      }
      if (r.type == R_SHUFFLE) { playFrom(-1, true); return true; }
      if (r.type == R_SONG) {
        if (view == V_QUEUE) Ctl::queue.erase(Ctl::queue.begin() + (std::find(Ctl::queue.begin(), Ctl::queue.end(), r.idx) - Ctl::queue.begin()));
        playFrom(r.idx, false);
      }
      return true;
    }
    if (e.code == K_CHAR && e.ch == ' ') { if (Music::active()) Music::pause(!Music::paused()); return true; }
    if (e.code == K_CHAR && e.fn && tolower(e.ch) == 'r') { Lib::scan(); Ctl::setSource({}, "ALL SONGS"); build(); toast("LIBRARY RESCANNED"); return true; }
    if (e.code == K_CHAR && e.ch == '\\' && view == V_SONGS) { sortMode = (sortMode + 1) % 4; build(); sel = 0; top = 0; skipHeader(1); toast((String("SORT: ") + SORTN[sortMode]).c_str(), 800); return true; }
    if (e.code == K_CHAR && (e.ch == '-' || e.ch == '=')) { volumeChange(e.ch == '=' ? 1 : -1); return true; }
    int s = n && rows[sel].type == R_SONG ? rows[sel].idx : -1;
    if (e.code == K_CHAR && s >= 0 && !e.fn) {
      char c = e.ch;
      if (c == '+') { Ctl::queue.push_back(s); toast("ADDED TO QUEUE"); return true; }
      if (c == '>') { Ctl::queue.insert(Ctl::queue.begin(), s); toast("PLAYS NEXT"); return true; }
      if (c == '*') {
        auto it = std::find(Lib::favs.begin(), Lib::favs.end(), Lib::files[s]);
        if (it == Lib::favs.end()) { Lib::favs.push_back(Lib::files[s]); toast("ADDED TO FAVOURITES"); }
        else { Lib::favs.erase(it); toast("REMOVED FROM FAVOURITES"); }
        Lib::saveFavs(); if (view == V_FAVS) build(); return true;
      }
      if (c == '&') { Lib::appendM3U("mix.m3u", Lib::files[s]); toast("ADDED TO MIX.M3U"); return true; }
    }
    if (view == V_QUEUE && e.code == K_BACK && s >= 0) {
      Ctl::queue.erase(std::find(Ctl::queue.begin(), Ctl::queue.end(), s));
      build(); return true;
    }
    if (view == V_QUEUE && e.code == K_CHAR && e.ch == '$' && !Ctl::queue.empty()) {
      ensureDir("/playlists");
      File f = SD.open("/playlists/queue.m3u", FILE_WRITE);
      if (f) { f.println("#EXTM3U"); for (int q : Ctl::queue) f.println(Lib::files[q]); f.close(); toast("SAVED QUEUE.M3U"); }
      return true;
    }
    if (e.code == K_CHAR && isalnum((unsigned char)e.ch) && !e.fn) {  // type-to-jump (title / artist / folder)
      char c = toupper(e.ch);
      for (int i = 0; i < n; i++) {
        const Row& r = rows[(sel + 1 + i) % n];
        if (r.type == R_HEADER || r.type == R_SHUFFLE) continue;
        String l = (view == V_SONGS && sortMode == 1) ? rowSub(r) : rowLabel(r);
        if (l.length() && toupper(l[0]) == c) { sel = (sel + 1 + i) % n; break; }
      }
      return true;
    }
    return false;
  }

  bool key(const KeyEv& e) override {
    if (help) { help = false; return true; }
    if (e.code == K_TAB) { setView(view >= V_COUNT ? V_PLAYING : (view + 1) % V_COUNT); return true; }
    if (e.code == K_CHAR && (e.ch == '?' || (e.ch == 'h' && view == V_PLAYING))) { help = true; return true; }
    if (view != V_PLAYING) return listKey(e);
    KeyCode k = e.nav();
    if (e.code == K_CHAR && e.ch == ' ') {
      if (!Music::active() && !Lib::files.empty()) Ctl::play(Ctl::cur < 0 ? Ctl::order[0] : Ctl::cur);
      else Music::pause(!Music::paused());
      return true;
    }
    if (k == K_LEFT) { Ctl::prev(); return true; }
    if (k == K_RIGHT) { Ctl::next(true); return true; }
    if (k == K_UP) { volumeChange(1); return true; }
    if (k == K_DOWN) { volumeChange(-1); return true; }
    if (e.code == K_ENTER) { setView(V_SONGS); return true; }
    if (e.code != K_CHAR || e.fn) return false;
    char c = tolower(e.ch);
    char b[40];
    switch (c) {
      case '-': case '_': volumeChange(-1); return true;
      case '=': case '+': volumeChange(1); return true;
      case '[': Music::seek(Music::pos() - 10); return true;
      case ']': Music::seek(Music::pos() + 10); return true;
      case 's': Ctl::shuffle = !Ctl::shuffle; Ctl::buildOrder(); toast(Ctl::shuffle ? "SHUFFLE ON" : "SHUFFLE OFF"); Ctl::saveState(); return true;
      case 'x': if (!Lib::files.empty()) { Ctl::shuffle = true; Ctl::buildOrder(); Ctl::next(true); toast("SHUFFLE: RANDOM SONG", 900); } return true;
      case 'r': Ctl::repeat = (Ctl::repeat + 1) % 3; snprintf(b, sizeof(b), "REPEAT %s", Ctl::RPT[Ctl::repeat]); toast(b); Ctl::saveState(); return true;
      case 'e': Ctl::eq = (Ctl::eq + 1) % Ctl::NEQ; Music::setEQ(Ctl::EQ[Ctl::eq].b, Ctl::EQ[Ctl::eq].t);
                snprintf(b, sizeof(b), "EQ %s", Ctl::EQ[Ctl::eq].n); toast(b); Ctl::saveState(); return true;
      case 'v': Ctl::vis = (Ctl::vis + 1) % 4; Ctl::lyricsView = false; Ctl::saveState(); return true;
      case 'l': if (Meta::lyrics.empty()) toast("NO .LRC LYRICS FILE"); else Ctl::lyricsView = !Ctl::lyricsView; return true;
      case 'a': Music::abPress(); toast(Music::abState() == 1 ? "A SET - PRESS A FOR B" : (Music::abState() == 2 ? "A-B REPEAT ON" : "A-B OFF")); return true;
      case '<': case ',': Ctl::speed = std::max(0.5f, Ctl::speed - 0.1f); Music::setSpeed(Ctl::speed); snprintf(b, sizeof(b), "SPEED %.1fX", Ctl::speed); toast(b); return true;
      case '>': case '.': Ctl::speed = std::min(2.0f, Ctl::speed + 0.1f); Music::setSpeed(Ctl::speed); snprintf(b, sizeof(b), "SPEED %.1fX", Ctl::speed); toast(b); return true;
      case '0': Ctl::speed = 1; Music::setSpeed(1); toast("SPEED 1.0X"); return true;
      case 'z': {
        const int opts[] = {0, 15, 30, 60, 90};
        int i = 0; while (i < 5 && opts[i] != Ctl::sleepMin) i++;
        Ctl::sleepMin = opts[(i + 1) % 5];
        Ctl::sleepAt = Ctl::sleepMin ? millis() + Ctl::sleepMin * 60000UL : 0;
        Music::setGain(1);
        if (Ctl::sleepMin) snprintf(b, sizeof(b), "SLEEP IN %d MIN", Ctl::sleepMin); else snprintf(b, sizeof(b), "SLEEP TIMER OFF");
        toast(b); return true;
      }
      case 'f':
        if (Ctl::cur >= 0) {
          String p = Lib::files[Ctl::cur];
          auto it = std::find(Lib::favs.begin(), Lib::favs.end(), p);
          if (it == Lib::favs.end()) { Lib::favs.push_back(p); toast("ADDED TO FAVOURITES"); }
          else { Lib::favs.erase(it); toast("REMOVED FROM FAVOURITES"); }
          Lib::saveFavs();
        }
        return true;
      case 'i': {
        uint32_t t = Stats::totalSec;
        snprintf(b, sizeof(b), "PLAYS %d  TOTAL %uH%02uM", Ctl::cur >= 0 ? Stats::playCount(Lib::files[Ctl::cur]) : 0, (unsigned)(t / 3600), (unsigned)((t / 60) % 60));
        toast(b, 2500); return true;
      }
    }
    return false;
  }

  void drawHeart(int x, int y, uint16_t c) {
    static const char* h[5] = {".#.#.", "#####", "#####", ".###.", "..#.."};
    for (int j = 0; j < 5; j++) for (int i = 0; i < 5; i++) if (h[j][i] == '#') cv.drawPixel(x + i, y + j, c);
  }

  void drawList() {
    background();
    String title = view == V_CTX ? ctxName : String(viewName(view));
    title.toUpperCase();
    statusBar(title.c_str(), T.cyan);
    if (view != V_CTX)
      for (int i = 1; i < V_COUNT; i++) cv.fillRect(150 + i * 8 - 70, 13, 6, 1, i == view ? T.cyan : T.dim);
    int n = rows.size();
    panel(3, 15, 234, 108, T.edge);
    int songs = 0;
    for (auto& r : rows) if (r.type == R_SONG) songs++;
    if (!n) {
      const char* a = "EMPTY", *b2 = "";
      if (view == V_SONGS) { a = "NO MUSIC FOUND"; b2 = "COPY MP3/FLAC/WAV TO /MUSIC"; }
      else if (view == V_FAVS) { a = "NO FAVOURITES YET"; b2 = "F WHILE PLAYING, * IN LISTS"; }
      else if (view == V_QUEUE) { a = "QUEUE IS EMPTY"; b2 = "+ ADDS A SONG, > PLAYS NEXT"; }
      textC(120, 50, a, T.white);
      textC(120, 64, b2, T.grey, 1, false);
    } else {
      sel = constrain(sel, 0, n - 1);
      const int vis = 9;
      if (sel < top) top = sel;
      if (sel >= top + vis) top = sel - vis + 1;
      if (top > 0 && rows[top].type != R_HEADER && rows[top - 1].type == R_HEADER && sel < top + vis - 1) top--;  // keep the header visible
      for (int r = 0; r < vis && top + r < n; r++) {
        const Row& row = rows[top + r];
        int y = 18 + r * 11;
        bool s = top + r == sel;
        String lbl = rowLabel(row);
        if (row.type == R_HEADER) {
          String h = lbl.length() > 30 ? lbl.substring(0, 30) : lbl;
          text(8, y + 2, h.c_str(), T.purp, 1, false);
          cv.drawFastHLine(12 + textW(h.c_str()), y + 5, std::max(0, 214 - textW(h.c_str())), T.purpD);
          continue;
        }
        String sub = rowSub(row);
        if (s) { cv.fillRect(5, y - 1, 222, 11, lerpc(T.panel, T.cyan, 0.18f)); cv.fillRect(5, y - 1, 2, 11, T.cyan); }
        if (row.type == R_SHUFFLE) {
          // two crossing arrows
          uint16_t c = T.green;
          cv.drawLine(10, y + 1, 16, y + 7, c); cv.drawLine(10, y + 7, 16, y + 1, c);
          cv.drawFastHLine(14, y + 1, 3, c); cv.drawFastHLine(14, y + 7, 3, c);
          text(22, y + 1, lbl.c_str(), s ? T.white : T.green, 1, false);
          text(222 - textW(sub.c_str()), y + 1, sub.c_str(), T.dim, 1, false);
        } else if (row.type == R_SONG) {
          bool playingThis = row.idx == Ctl::cur && Music::active();
          if (playingThis) text(10, y + 1, Music::paused() ? "=" : ">", T.cyan, 1, false);
          bool showArtist = !(view == V_CTX && ctxFrom == V_FOLDERS) && !(view == V_SONGS && sortMode == 1);
          int tw = showArtist ? 128 : 190;
          textFit(18, y + 1, lbl.c_str(), s ? T.white : (playingThis ? T.cyan : T.grey), tw, false);
          if (showArtist) textFit(150, y + 1, up(sub).c_str(), s ? T.cyan : T.dim, 64, false);
          if (Lib::isFav(Lib::files[row.idx])) drawHeart(220, y + 2, T.pink);
        } else if (row.type == R_FOLDER) {
          cv.fillRect(10, y + 2, 8, 6, T.yel); cv.fillRect(10, y + 1, 3, 1, T.yel);
          textFit(22, y + 1, lbl.c_str(), s ? T.white : T.grey, 170, false);
          text(222 - textW(sub.c_str()), y + 1, sub.c_str(), T.dim, 1, false);
        } else {
          text(10, y + 1, row.idx < 2 ? "*" : "#", row.idx < 2 ? T.purp : T.green, 1, false);
          textFit(22, y + 1, lbl.c_str(), s ? T.white : T.grey, 190, false);
        }
      }
      if (n > vis) {
        int h = std::max(6, 100 * vis / n);
        int y = 18 + (100 - h) * top / std::max(1, n - vis);
        cv.fillRect(231, 18, 2, 100, T.dim);
        cv.fillRect(231, y, 2, h, T.cyan);
      }
    }
    char b[24]; snprintf(b, sizeof(b), "%d SONGS", songs);
    if (view == V_SONGS) hintBar({{"OK", "PLAY"}, {"\\", SORTN[sortMode]}, {"-=", "VOL"}, {"TAB", "VIEW"}});
    else if (view == V_FOLDERS || view == V_LISTS) hintBar({{"OK", "OPEN"}, {"A-Z", "JUMP"}, {"TAB", "VIEW"}});
    else if (view == V_QUEUE) hintBar({{"OK", "PLAY"}, {"DEL", "REMOVE"}, {"$", "SAVE"}, {"TAB", "VIEW"}});
    else hintBar({{"OK", "PLAY"}, {"+", "QUEUE"}, {"*", "FAV"}, {"", b}});
  }

  void computeBars() {
    static int16_t s[256];
    Audio::getScope(s, 256, true);
    static const float freqs[16] = {60, 90, 130, 190, 280, 400, 580, 840, 1200, 1700, 2400, 3300, 4600, 6200, 8500, 11000};
    float sum = 0;
    for (int i = 0; i < 256; i++) sum += (float)s[i] * s[i];
    float rms = sqrtf(sum / 256) / 32768.f;
    vu += (std::min(1.f, rms * 3) - vu) * 0.3f;
    for (int b = 0; b < 16; b++) {
      float w = 2 * cosf(2 * PI * freqs[b] / Audio::SR), q1 = 0, q2 = 0;
      for (int i = 0; i < 256; i++) { float q0 = w * q1 - q2 + s[i] / 32768.f; q2 = q1; q1 = q0; }
      float mag = sqrtf(std::max(0.f, q1 * q1 + q2 * q2 - w * q1 * q2)) / 64.f;
      float v = constrain(log10f(1 + mag * 30) * 1.2f, 0.f, 1.f);
      bars[b] = v > bars[b] ? v : bars[b] * 0.82f;
      peaks[b] = std::max(peaks[b] - 0.015f, bars[b]);
    }
  }

  void drawArt(int x, int y, float ph) {
    panel(x, y, 60, 60, T.cyanD, T.bg0);
    for (int px = x + 2; px < x + 58; px++) {
      float t = (px - x - 2) / 56.f;
      int py = y + 30 - (int)(14 * sinf(2 * PI * t * 0.8f - 2.2f + ph) * (0.6f + vu));
      cv.drawPixel(px, py, gradc(t));
      cv.drawPixel(px, py + 1, lerpc(T.bg0, gradc(t), 0.4f));
      cv.drawPixel(px, py + (int)(5 * sinf(PI * t)), lerpc(T.purp, T.bg0, 0.2f));
    }
  }

  void drawPlaying() {
    background();
    statusBar("NOW PLAYING", T.cyan);
    bool act = Music::active();
    static float ph = 0;
    if (act && !Music::paused()) ph += 0.05f * Ctl::speed;
    computeBars();
    float L = Music::len(), P = Music::pos();

    if (Ctl::vis == 3 && !Ctl::lyricsView) {  // big "car mode"
      String t = Meta::title; t.toUpperCase();
      if (t.length() > 19) { int sh = ((millis() - enterT) / 300) % (t.length() + 4); String m = t + "    " + t; t = m.substring(sh, sh + 19); }
      text(6, 18, t.c_str(), T.white, 2);
      String a = Meta::artist; a.toUpperCase();
      textFit(6, 38, a.c_str(), T.cyan, 228);
      String tm = fmtTime((uint32_t)P);
      textC(120, 54, tm.c_str(), T.white, 4);
      int pw = 228, pp = L > 0 ? (int)(pw * P / L) : 0;
      cv.fillRect(6, 92, pw, 4, T.dim);
      for (int x = 0; x < pp; x++) cv.drawFastVLine(6 + x, 92, 4, gradc(x / (float)pw));
      textC(120, 104, Music::paused() || !act ? "PAUSED" : "", T.yel, 2);
      hintBar({{"SPC", "PLAY"}, {"<>", "SKIP"}, {"^v", "VOL"}, {"V", "MODE"}});
      return;
    }

    drawArt(6, 16, ph);
    if (Ctl::cur >= 0 && Lib::isFav(Lib::files[Ctl::cur])) drawHeart(57, 19, T.pink);
    if (Ctl::cur >= 0) {
      String title = Meta::title; title.toUpperCase();
      int maxc = 26;
      if ((int)title.length() > maxc) {
        int sh = ((millis() - enterT) / 250) % (title.length() + 4);
        String m = title + "    " + title;
        title = m.substring(sh, sh + maxc);
      }
      text(72, 18, title.c_str(), T.white);
      String art = Meta::artist; art.toUpperCase();
      if (Meta::album.length()) { String al = Meta::album; al.toUpperCase(); art += " - " + al; }
      textFit(72, 29, art.c_str(), T.cyan, 162);
      char fmt[48];
      snprintf(fmt, sizeof(fmt), "%s %dK %dB", Music::format(), Music::srcRate() / 1000, Music::bits());
      text(72, 40, fmt, T.grey, 1, false);
      if (!Meta::lyrics.empty()) text(220, 40, "LRC", Ctl::lyricsView ? T.green : T.dim, 1, false);
    } else {
      text(72, 22, Lib::files.empty() ? "NO MUSIC" : "READY", T.white);
      text(72, 34, "OK = BROWSE SONGS", T.grey, 1, false);
    }
    int vx = 72, vy = 86;
    if (Ctl::lyricsView && !Meta::lyrics.empty()) {
      int k = Meta::lyricIdx(P);
      for (int d = -1; d <= 1; d++) {
        int i = k + d;
        if (i < 0 || i >= (int)Meta::lyrics.size()) continue;
        String l = Meta::lyrics[i].s; l.toUpperCase();
        textFit(vx, 54 + (d + 1) * 11, l.c_str(), d == 0 ? T.white : T.dim, 162, d == 0);
      }
    } else if (Ctl::vis == 0) {
      for (int b = 0; b < 16; b++) {
        int h = (int)(bars[b] * 32);
        int x = vx + b * 10;
        for (int j = 0; j < h; j += 2) {
          uint16_t c = gradc(j / 32.f);
          cv.fillRect(x, vy - j, 7, 1, c);
          cv.fillRect(x + 7, vy - j, 1, 1, lerpc(c, T.bg0, 0.5f));
        }
        cv.fillRect(x, vy - (int)(peaks[b] * 32) - 2, 7, 1, T.white);
      }
    } else if (Ctl::vis == 1) {
      int16_t s[160];
      Audio::getScope(s, 160, true);
      int py = vy - 16;
      for (int i = 0; i < 160; i++) {
        int y = vy - 16 - s[i] * 16 / 32768;
        cv.drawLine(vx + i - 1, py, vx + i, y, gradc(i / 160.f));
        py = y;
      }
    } else {
      panel(vx, 52, 162, 34, T.edge, T.bg0);
      int w = (int)(vu * 150);
      for (int i = 0; i < 150; i += 3) {
        uint16_t c = i < 100 ? gradc(i / 150.f) : (i < 130 ? T.yel : T.red);
        cv.fillRect(vx + 6 + i, 60, 2, 18, i < w ? c : T.dim);
      }
    }
    // progress
    text(6, 92, fmtTime((uint32_t)P).c_str(), T.grey, 1, false);
    String ls = fmtTime((uint32_t)L);
    text(234 - textW(ls.c_str()), 92, ls.c_str(), T.grey, 1, false);
    int px0 = 40, pw = 160;
    cv.drawFastHLine(px0, 95, pw, T.dim);
    int pp = L > 0 ? constrain((int)(pw * P / L), 0, pw) : 0;
    for (int x = 0; x < pp; x++) { cv.drawPixel(px0 + x, 95, gradc(x / (float)pw)); cv.drawPixel(px0 + x, 96, lerpc(T.bg0, gradc(x / (float)pw), 0.5f)); }
    if (Music::abState() >= 1 && L > 0) cv.fillRect(px0 + (int)(pw * Music::abA() / L), 92, 1, 7, T.yel);
    if (Music::abState() == 2 && L > 0) cv.fillRect(px0 + (int)(pw * Music::abB() / L), 92, 1, 7, T.yel);
    cv.fillRect(px0 + pp - 1, 93, 3, 5, T.white);
    if (Music::seeking()) textC(120, 84, "SEEKING...", T.yel, 1);
    int cx = 120;
    panel(cx - 9, 101, 18, 15, T.cyan);
    if (!act || Music::paused()) { for (int i = 0; i < 5; i++) cv.drawFastVLine(cx - 2 + i, 106 + i / 2, 5 - (i / 2) * 2, T.white); }
    else { cv.fillRect(cx - 3, 105, 2, 7, T.white); cv.fillRect(cx + 1, 105, 2, 7, T.white); }
    static const char* pv[5] = {"#..#", "#.##", "####", "#.##", "#..#"};
    static const char* nx[5] = {"#..#", "##.#", "####", "##.#", "#..#"};
    for (int j = 0; j < 5; j++) for (int i = 0; i < 4; i++) {
      if (pv[j][i] == '#') cv.drawPixel(cx - 30 + i, 106 + j, T.grey);
      if (nx[j][i] == '#') cv.drawPixel(cx + 27 + i, 106 + j, T.grey);
    }
    char b[16];
    text(6, 104, "SHUF", Ctl::shuffle ? T.purp : T.dim, 1, false);
    snprintf(b, sizeof(b), "RPT%s", Ctl::repeat == 2 ? "1" : (Ctl::repeat ? "" : "-"));
    text(6, 114, b, Ctl::repeat ? T.purp : T.dim, 1, false);
    if (!Ctl::queue.empty()) { snprintf(b, sizeof(b), "Q%d", (int)Ctl::queue.size()); text(40, 114, b, T.green, 1, false); }
    text(160, 104, Ctl::EQ[Ctl::eq].n, Ctl::eq ? T.yel : T.dim, 1, false);
    snprintf(b, sizeof(b), "%.1fX", Ctl::speed);
    text(206, 104, b, Ctl::speed != 1 ? T.green : T.dim, 1, false);
    if (Ctl::sleepAt) { snprintf(b, sizeof(b), "Z%dM", (int)((Ctl::sleepAt - millis()) / 60000) + 1); text(160, 114, b, T.blue, 1, false); }
    if (Music::abState()) text(200, 114, Music::abState() == 2 ? "A-B" : "A-", T.yel, 1, false);
    hintBar({{"SPC", "PLAY"}, {"<>", "SKIP"}, {"-=", "VOL"}, {"S", "SHUF"}, {"H", "HELP"}});
  }

  void draw() override {
    if (view == V_PLAYING) drawPlaying();
    else drawList();
    if (help)
      helpOverlay("MUSIC KEYS", {"SPC  PLAY/PAUSE", "< >  PREV/NEXT", "- = ^v  VOLUME", "X  RANDOM SONG", "[ ]  SEEK 10S", "S  SHUFFLE", "R  REPEAT", "E  EQ PRESET",
                                 "V  VISUAL/BIG", "L  LYRICS", "A  A-B REPEAT", "SHIFT< >  SPEED", "Z  SLEEP TIMER", "F  FAVOURITE", "I  STATS",
                                 "TAB  LIBRARY", "+ >  QUEUE/NEXT", "*  FAV (LIST)", "&  ADD TO MIX", "SHAKE  RANDOM"});
  }
};

App* appMusic = new MusicApp();
