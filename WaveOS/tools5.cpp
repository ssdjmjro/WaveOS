// WAVE OS - FILES (in TOOLS): a file manager for the SD card
//   browse folders, see sizes, sort by name / size / date, view text files, play songs, open notes,
//   rename, delete (files and whole folders, always with a Y confirm) and clean up junk files
//   (macOS "._" copies, .DS_Store, Thumbs.db, empty 0-byte files) in one go.
// Deleting inside /waveos (settings, saves) asks twice. The song that is playing can't be deleted.
#include "common.h"
#include "audio.h"

static bool isJunkName(const char* n, uint32_t size, bool dir) {
  if (dir) return false;
  return !strncmp(n, "._", 2) || !strcasecmp(n, ".DS_Store") || !strcasecmp(n, "Thumbs.db") || !strcasecmp(n, "desktop.ini") || size == 0;
}
static const char* extOf(const char* n) { const char* d = strrchr(n, '.'); return d ? d + 1 : ""; }
static bool isAudio(const char* n) {
  const char* e = extOf(n);
  return !strcasecmp(e, "mp3") || !strcasecmp(e, "wav") || !strcasecmp(e, "flac") || !strcasecmp(e, "aac") || !strcasecmp(e, "m4a");
}
static bool isText(const char* n) {
  static const char* T_[] = {"txt", "md", "csv", "ir", "m3u", "log", "json", "ini", "cfg", "sav", "sub", "lrc", "html", "xml", "py", "c", "h", "cpp"};
  const char* e = extOf(n);
  for (auto t : T_) if (!strcasecmp(e, t)) return true;
  return false;
}
static void fmtSize(char* b, int n, uint32_t s) {
  if (s < 1024) snprintf(b, n, "%uB", (unsigned)s);
  else if (s < 1024 * 1024) snprintf(b, n, "%.1fK", s / 1024.f);
  else snprintf(b, n, "%.1fM", s / 1048576.f);
}

struct FilesApp : App {
  struct Ent { String name; uint32_t size; uint32_t t; bool dir, junk; };
  std::vector<Ent> ents;
  char dir[128] = "/";
  int sel = 0, top = 0, sortMode = 0, junkN = 0;
  bool truncated = false, help = false;
  enum { BROWSE, CONFIRM, CONFIRM2, RENAME, VIEW, INFO } mode = BROWSE;
  enum { A_DEL, A_JUNK } act = A_DEL;
  String input, confirmMsg;
  int delCount = 0;
  // text viewer
  std::vector<String> vlines; int vtop = 0; String vname;
  FilesApp() { name = "FILES"; }
  const char* saveKey() override { return "files"; }
  void persist(Persist& p) override {
    p.io(dir); p.io(sortMode);
    if (p.loading) { dir[sizeof(dir) - 1] = 0; if (dir[0] != '/') strcpy(dir, "/"); sortMode %= 3; }
  }
  bool wantsAllKeys() override { return mode == RENAME; }

  String full(const Ent& e) const { String p = dir; if (!p.endsWith("/")) p += "/"; return p + e.name; }
  bool atRoot() const { return !strcmp(dir, "/"); }

  void scan(const char* keepName = nullptr) {
    ents.clear(); junkN = 0; truncated = false;
    if (!sdOK) return;
    File d = SD.open(dir);
    if (!d || !d.isDirectory()) { strcpy(dir, "/"); d = SD.open("/"); if (!d) return; }
    for (;;) {
      if (ents.size() >= 400 || ESP.getMaxAllocHeap() < 12000) { truncated = true; break; }
      File f = d.openNextFile();
      if (!f) break;
      const char* n = f.name();
      const char* slash = strrchr(n, '/');
      if (slash) n = slash + 1;
      Ent e{String(n), (uint32_t)(f.isDirectory() ? 0 : f.size()), (uint32_t)f.getLastWrite(), f.isDirectory(), false};
      e.junk = isJunkName(n, e.size, e.dir);
      if (e.junk) junkN++;
      f.close();
      ents.push_back(std::move(e));
      if ((ents.size() & 31) == 0) yield();
    }
    d.close();
    sortEnts();
    sel = 0; top = 0;
    if (keepName) for (int i = 0; i < (int)ents.size(); i++) if (ents[i].name == keepName) sel = i;
  }
  void sortEnts() {
    std::sort(ents.begin(), ents.end(), [&](const Ent& a, const Ent& b) {
      if (a.dir != b.dir) return a.dir;
      if (sortMode == 1 && a.size != b.size) return a.size > b.size;
      if (sortMode == 2 && a.t != b.t) return a.t > b.t;
      return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });
  }
  void enter() override { mode = BROWSE; help = false; scan(); }
  void leave() override { ents.clear(); ents.shrink_to_fit(); vlines.clear(); vlines.shrink_to_fit(); }

  void goUp() {
    if (atRoot()) return;
    String d = dir; int s = d.lastIndexOf('/');
    String child = d.substring(s + 1);
    d = s <= 0 ? "/" : d.substring(0, s);
    strlcpy(dir, d.c_str(), sizeof(dir));
    scan(child.c_str());
  }
  void openSel() {
    if (ents.empty()) return;
    Ent& e = ents[sel];
    String p = full(e);
    if (e.dir) {
      if (p.length() >= sizeof(dir)) { toast("PATH TOO LONG"); return; }
      strlcpy(dir, p.c_str(), sizeof(dir)); scan(); return;
    }
    if (isAudio(e.name.c_str())) {
      int i = songIndex(p);
      if (i >= 0) { playSong(i); return; }
      toast("NOT IN THE MUSIC LIBRARY (ONLY /MUSIC)", 1500); return;
    }
    if (p.startsWith("/notes/") && (p.endsWith(".md") || p.endsWith(".txt"))) { openNote(e.name); return; }
    if (isText(e.name.c_str()) || e.size < 4096) { view(p, e.name); return; }
    mode = INFO;
  }
  void view(const String& p, const String& nm) {
    File f = SD.open(p);
    if (!f) { toast("CAN'T OPEN"); return; }
    vlines.clear(); vtop = 0; vname = nm;
    String cur;
    int read = 0;
    while (f.available() && read < 12000 && vlines.size() < 600) {
      int c = f.read(); read++;
      if (c == '\r') continue;
      if (c == '\n') { vlines.push_back(cur); cur = ""; continue; }
      cur += (c >= 32 && c < 127) ? (char)c : (c == '\t' ? ' ' : '.');
      if (cur.length() >= 39) { vlines.push_back(cur); cur = ""; }
    }
    if (cur.length()) vlines.push_back(cur);
    if (f.available()) vlines.push_back("... (FILE CONTINUES)");
    f.close();
    mode = VIEW;
  }

  // ---- delete
  int countTree(const String& p, int depth = 0) {  // files + folders inside (to show before deleting)
    File d = SD.open(p);
    if (!d) return 0;
    int n = 0;
    for (File f = d.openNextFile(); f; f = d.openNextFile()) {
      n++;
      if (f.isDirectory() && depth < 8) { String c = f.path(); f.close(); n += countTree(c, depth + 1); }
      else f.close();
      if (n > 5000) break;
    }
    d.close();
    return n;
  }
  bool removeTree(const String& p, int depth = 0) {
    File d = SD.open(p);
    if (!d) return false;
    if (!d.isDirectory()) { d.close(); return SD.remove(p); }
    std::vector<String> kids; std::vector<bool> kdir;
    for (File f = d.openNextFile(); f; f = d.openNextFile()) { kids.push_back(f.path()); kdir.push_back(f.isDirectory()); f.close(); }
    d.close();
    bool ok = true;
    for (size_t i = 0; i < kids.size(); i++) {
      ok &= kdir[i] ? (depth < 8 && removeTree(kids[i], depth + 1)) : SD.remove(kids[i]);
      yield();
    }
    return SD.rmdir(p) && ok;
  }
  bool protectedPath(const String& p) { return p == "/waveos" || p.startsWith("/waveos/"); }
  void askDelete() {
    if (ents.empty()) return;
    Ent& e = ents[sel];
    String p = full(e);
    if (!e.dir && p == nowPlayingPath() && Music::active()) { toast("THIS SONG IS PLAYING", 1200); return; }
    act = A_DEL;
    if (e.dir) { delCount = countTree(p); confirmMsg = "DELETE FOLDER + " + String(delCount) + " ITEMS?"; }
    else confirmMsg = "DELETE THIS FILE?";
    mode = protectedPath(p) ? CONFIRM2 : CONFIRM;
  }
  void doDelete() {
    Ent e = ents[sel];
    String p = full(e);
    bool ok = e.dir ? removeTree(p) : SD.remove(p);
    toast(ok ? "DELETED" : "COULDN'T DELETE ALL", 1000);
    int s = sel;
    scan();
    sel = std::min(s, std::max(0, (int)ents.size() - 1));
    if (p.startsWith("/music")) toast("DELETED. MUSIC: FN+R TO REFRESH", 1500);
  }
  void doJunk() {
    int n = 0;
    for (auto& e : ents) if (e.junk && SD.remove(full(e))) n++;
    char b[32]; snprintf(b, sizeof(b), "REMOVED %d JUNK FILES", n); toast(b, 1200);
    scan();
  }

  bool key(const KeyEv& e) override {
    if (help) { help = false; return true; }
    if (mode == VIEW) {
      KeyCode k = e.nav();
      int page = 9, maxTop = std::max(0, (int)vlines.size() - page);
      if (k == K_UP) vtop = std::max(0, vtop - 1);
      else if (k == K_DOWN) vtop = std::min(maxTop, vtop + 1);
      else if (k == K_LEFT) vtop = std::max(0, vtop - page);
      else if (k == K_RIGHT || (e.code == K_CHAR && e.ch == ' ')) vtop = std::min(maxTop, vtop + page);
      else { mode = BROWSE; vlines.clear(); vlines.shrink_to_fit(); }
      return true;
    }
    if (mode == INFO) { mode = BROWSE; return true; }
    if (mode == CONFIRM || mode == CONFIRM2) {
      if (e.is('y')) {
        if (mode == CONFIRM2) { mode = CONFIRM; confirmMsg = "SYSTEM FILE! REALLY DELETE?"; return true; }
        mode = BROWSE;
        if (act == A_JUNK) doJunk(); else doDelete();
      } else { mode = BROWSE; toast("KEPT", 600); }
      return true;
    }
    if (mode == RENAME) {
      if (e.code == K_ESC) { mode = BROWSE; return true; }
      if (e.code == K_BACK) { if (input.length()) input.remove(input.length() - 1); return true; }
      if (e.code == K_ENTER) {
        input.trim();
        if (!input.length() || input.indexOf('/') >= 0) { toast("BAD NAME"); return true; }
        String from = full(ents[sel]);
        String to = String(dir) + (atRoot() ? "" : "/") + input;
        if (SD.exists(to)) { toast("NAME ALREADY USED"); return true; }
        bool ok = SD.rename(from, to);
        toast(ok ? "RENAMED" : "RENAME FAILED");
        mode = BROWSE;
        if (ok) { String keep = input; scan(keep.c_str()); }
        return true;
      }
      if (e.code == K_CHAR && !e.fn && e.ch >= 32 && e.ch < 127 && input.length() < 60) input += e.ch;
      return true;
    }
    // ---- browse
    KeyCode k = e.nav();
    int n = ents.size();
    if (k == K_UP && n) { sel = (sel + n - 1) % n; return true; }
    if (k == K_DOWN && n) { sel = (sel + 1) % n; return true; }
    if (k == K_RIGHT || e.code == K_ENTER) { openSel(); return true; }
    if (k == K_LEFT || (e.code == K_ESC && !atRoot())) { goUp(); return true; }
    if (e.code == K_BACK && e.fn) { askDelete(); return true; }
    if (e.code == K_BACK && !atRoot()) { goUp(); return true; }
    if (e.code != K_CHAR || e.fn) return false;
    switch (tolower(e.ch)) {
      case 'd': askDelete(); return true;
      case 'r': if (n) { input = ents[sel].name; mode = RENAME; } return true;
      case 'i': if (n) mode = INFO; return true;
      case 's': { static const char* SN[3] = {"SORT: NAME", "SORT: BIGGEST", "SORT: NEWEST"}; sortMode = (sortMode + 1) % 3;
                  String keep = n ? ents[sel].name : String(); sortEnts(); for (int i = 0; i < n; i++) if (ents[i].name == keep) sel = i; toast(SN[sortMode], 700); return true; }
      case 'j': if (junkN) { act = A_JUNK; char b[40]; snprintf(b, sizeof(b), "DELETE %d JUNK FILES HERE?", junkN); confirmMsg = b; mode = CONFIRM; }
                else toast("NO JUNK FILES HERE", 800);
                return true;
      case 'h': case '?': help = true; return true;
    }
    if ((e.ch >= '0' && e.ch <= '9') || isalpha((unsigned char)e.ch)) {  // jump to the next name with this letter
      for (int i = 1; i <= n; i++) { int j = (sel + i) % n; if (tolower(ents[j].name[0]) == tolower(e.ch)) { sel = j; break; } }
      return true;
    }
    return false;
  }

  void drawIconRow(int x, int y, const Ent& e, uint16_t c) {
    if (e.dir) { cv.fillRect(x, y + 1, 3, 1, c); cv.drawRect(x, y + 2, 8, 5, c); return; }
    cv.drawRect(x + 1, y, 6, 8, c); cv.drawFastHLine(x + 2, y + 3, 4, lerpc(c, T.bg0, 0.4f)); cv.drawFastHLine(x + 2, y + 5, 4, lerpc(c, T.bg0, 0.4f));
  }
  void draw() override {
    background();
    if (mode == VIEW) {
      String t = vname; t.toUpperCase();
      statusBar(t.c_str(), T.yel);
      panel(2, 14, 236, 107, T.edge, T.bg0);
      for (int r = 0; r < 9 && vtop + r < (int)vlines.size(); r++) text(6, 17 + r * 11, vlines[vtop + r].c_str(), T.white, 1, false);
      if (vlines.size() > 9) { int h = std::max(6, 100 * 9 / (int)vlines.size()); int y = 16 + (100 - h) * vtop / std::max(1, (int)vlines.size() - 9); cv.fillRect(234, y, 2, h, T.yel); }
      hintBar({{"^v", "SCROLL"}, {"<>", "PAGE"}, {"ANY", "BACK"}});
      return;
    }
    statusBar("FILES", T.yel);
    // path + counts
    char hdr[48]; snprintf(hdr, sizeof(hdr), "%d ITEMS%s", (int)ents.size(), truncated ? "+" : "");
    int hw = textW(hdr);
    textFit(4, 14, dir, T.cyan, 232 - hw - 8, false);
    text(236 - hw, 14, hdr, T.dim, 1, false);
    if (!sdOK) textC(120, 60, "NO SD CARD", T.red, 2);
    else if (ents.empty()) textC(120, 60, "EMPTY FOLDER", T.dim, 1, false);
    const int ROWS = 8, y0 = 25;
    if (sel < top) top = sel;
    if (sel >= top + ROWS) top = sel - ROWS + 1;
    for (int r = 0; r < ROWS && top + r < (int)ents.size(); r++) {
      const Ent& e = ents[top + r];
      int y = y0 + r * 12;
      bool s = top + r == sel;
      if (s) { cv.fillRect(2, y - 2, 236, 12, lerpc(T.panel, T.yel, 0.2f)); cv.fillRect(2, y - 2, 2, 12, T.yel); }
      uint16_t c = e.junk ? T.red : e.dir ? T.cyan : isAudio(e.name.c_str()) ? T.purp : s ? T.white : T.grey;
      drawIconRow(7, y, e, c);
      char sz[12] = "";
      if (!e.dir) fmtSize(sz, sizeof(sz), e.size);
      int sw = textW(sz);
      textFit(19, y, e.name.c_str(), s ? T.white : c, 236 - 19 - sw - 8, false);
      if (sz[0]) text(234 - sw, y, sz, e.junk ? T.red : T.dim, 1, false);
    }
    if (ents.size() > (size_t)ROWS) {
      int h = std::max(5, 96 * ROWS / (int)ents.size());
      int y = y0 - 2 + (96 - h) * top / std::max(1, (int)ents.size() - ROWS);
      cv.fillRect(238, y, 2, h, T.dim);
    }
    if (junkN) {
      char b[40]; snprintf(b, sizeof(b), "%d JUNK FILE%s (RED): J CLEANS", junkN, junkN > 1 ? "S" : "");
      cv.fillRect(0, 112, 240, 10, T.bg0); textC(120, 113, b, T.red, 1, false);
    }
    hintBar({{"OK", "OPEN"}, {"<", "UP"}, {"D", "DELETE"}, {"R", "RENAME"}, {"S", "SORT"}, {"?", "KEYS"}});
    if (mode == CONFIRM || mode == CONFIRM2) {
      panel(16, 40, 208, 52, T.red, T.bg1);
      textC(120, 46, mode == CONFIRM2 ? "WAVE OS SYSTEM FILE" : confirmMsg.c_str(), T.red, 1, false);
      if (act == A_DEL && !ents.empty()) textFit(24, 60, ents[sel].name.c_str(), T.white, 192, false);
      else textC(120, 60, "._ FILES, .DS_STORE, 0-BYTE FILES", T.grey, 1, false);
      textC(120, 77, mode == CONFIRM2 ? "SETTINGS / SAVES. Y = CONTINUE" : "Y = DELETE   ANY KEY = KEEP", T.grey, 1, false);
    }
    if (mode == RENAME) {
      panel(10, 44, 220, 40, T.yel, T.bg1);
      text(16, 49, "NEW NAME (OK = SAVE, ESC = CANCEL)", T.yel, 1, false);
      String s = input + ((millis() / 400) % 2 ? "_" : " ");
      int w = textW(s.c_str());
      text(16 + std::min(0, 204 - w), 66, s.c_str(), T.white, 1, false);
    }
    if (mode == INFO && !ents.empty()) {
      const Ent& e = ents[sel];
      panel(10, 30, 220, 76, T.cyan, T.bg1);
      textFit(16, 35, e.name.c_str(), T.white, 208, false);
      char b[48], sz[12];
      fmtSize(sz, sizeof(sz), e.size);
      snprintf(b, sizeof(b), "%s  %s", e.dir ? "FOLDER" : "FILE", e.dir ? "" : sz); text(16, 49, b, T.grey, 1, false);
      if (e.t > 1000000000UL) { time_t tt = e.t; struct tm* tm = gmtime(&tt); snprintf(b, sizeof(b), "CHANGED %04d-%02d-%02d %02d:%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min); text(16, 61, b, T.grey, 1, false); }
      if (e.junk) text(16, 73, e.size == 0 ? "EMPTY FILE (JUNK)" : "JUNK: A COMPUTER'S HIDDEN FILE", T.red, 1, false);
      textC(120, 92, "ANY KEY = CLOSE", T.dim, 1, false);
    }
    if (help)
      helpOverlay("FILES", {"^v  MOVE", "OK >  OPEN / PLAY / VIEW", "< DEL ESC  FOLDER UP", "D FN+DEL  DELETE (Y)", "R  RENAME", "I  INFO",
                            "S  SORT NAME/SIZE/DATE", "J  DELETE JUNK FILES", "A-Z  JUMP TO LETTER", "RED = JUNK FILE"});
  }
};

App* appFiles = new FilesApp();
