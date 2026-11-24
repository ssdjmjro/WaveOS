// WAVE OS - notes: markdown files in /notes on the SD card
#include "common.h"
#include "audio.h"
#include <algorithm>

struct NotesApp : App {
  enum { LIST, EDIT, NAME, CONFIRM_DEL };
  int mode = LIST;
  std::vector<String> files;
  int sel = 0;
  String path, buf, nameIn;
  int cur = 0, scroll = 0;
  bool modified = false;
  static constexpr int COLS = 37, ROWS = 11;
  struct VL { int start, len; };
  std::vector<VL> lines;
  NotesApp() { name = "NOTES"; }
  bool wantsAllKeys() override { return mode == EDIT || mode == NAME; }

  void scan() {
    files.clear();
    File d = SD.open("/notes");
    if (!d) return;
    while (true) {
      File f = d.openNextFile();
      if (!f) break;
      String n = f.name();
      if (!f.isDirectory() && !n.startsWith(".")) files.push_back(n);
      f.close();
    }
    d.close();
    std::sort(files.begin(), files.end());
  }
  void enter() override { if (mode == LIST) scan(); }
  void leave() override { if (mode == EDIT) save(); }
  uint32_t lastAuto = 0;
  void tick() override {  // autosave while typing (power-off safe)
    if (mode == EDIT && modified && millis() - lastAuto > 30000) { lastAuto = millis(); File f = SD.open(path, FILE_WRITE); if (f) { f.print(buf); f.close(); modified = false; } }
  }

  void open(const String& p) {
    path = p;
    buf = "";
    File f = SD.open(p);
    if (f) { buf.reserve(f.size() + 64); while (f.available() && buf.length() < 16000) buf += (char)f.read(); f.close(); }
    buf.replace("\r", "");
    cur = buf.length(); scroll = 0; modified = false;
    mode = EDIT;
  }
  void save() {
    if (!modified || !sdOK) return;
    File f = SD.open(path, FILE_WRITE);
    if (f) { f.print(buf); f.close(); modified = false; toast("SAVED"); }
    else toast("SAVE FAILED");
  }
  void layout() {
    lines.clear();
    int i = 0, n = buf.length();
    while (i <= n) {
      int nl = buf.indexOf('\n', i);
      int end = nl < 0 ? n : nl;
      int s = i;
      if (end - s == 0) lines.push_back({s, 0});
      while (end - s > 0) {
        int len = std::min(COLS, end - s);
        if (end - s > COLS) {  // word wrap
          int sp = buf.lastIndexOf(' ', s + len);
          if (sp > s) len = sp - s + 1;
        }
        lines.push_back({s, len});
        s += len;
      }
      if (nl < 0) break;
      i = nl + 1;
      if (i == n) { lines.push_back({n, 0}); break; }
    }
    if (lines.empty()) lines.push_back({0, 0});
  }
  int curLine() {
    for (int i = lines.size() - 1; i >= 0; i--) if (cur >= lines[i].start) return i;
    return 0;
  }
  int words() {
    int w = 0; bool in = false;
    for (unsigned i = 0; i < buf.length(); i++) { bool c = !isspace((unsigned char)buf[i]); if (c && !in) w++; in = c; }
    return w;
  }

  bool key(const KeyEv& e) override {
    if (mode == CONFIRM_DEL) {
      if (e.is('y')) { SD.remove("/notes/" + files[sel]); scan(); sel = std::max(0, sel - 1); toast("DELETED"); }
      mode = LIST;
      return true;
    }
    if (mode == NAME) {
      if (e.code == K_ESC) { mode = LIST; return true; }
      if (e.code == K_ENTER) {
        String n = nameIn; n.trim();
        if (!n.length()) return true;
        n.replace("/", "-");
        if (!n.endsWith(".md") && !n.endsWith(".txt")) n += ".md";
        String p = "/notes/" + n;
        if (!SD.exists(p)) { File f = SD.open(p, FILE_WRITE); if (f) { f.printf("# %s\n\n", nameIn.c_str()); f.close(); } }
        open(p);
        return true;
      }
      if (e.code == K_BACK) { if (nameIn.length()) nameIn.remove(nameIn.length() - 1); return true; }
      if (e.code == K_CHAR && !e.fn && nameIn.length() < 24) nameIn += e.ch;
      return true;
    }
    if (mode == LIST) {
      KeyCode k = e.nav();
      int n = files.size();
      if (k == K_UP && n) { sel = (sel + n - 1) % n; return true; }
      if (k == K_DOWN && n) { sel = (sel + 1) % n; return true; }
      if (k == K_ENTER && n) { open("/notes/" + files[sel]); return true; }
      if (e.is('n')) { if (!sdOK) { toast("NO SD CARD"); return true; } nameIn = ""; mode = NAME; return true; }
      if ((e.is('d') || (e.code == K_BACK && e.fn)) && n) { mode = CONFIRM_DEL; return true; }
      return false;
    }
    // ---- editor
    layout();
    KeyCode k = e.navFn();
    int ln = curLine();
    if (e.code == K_ESC) { save(); mode = LIST; scan(); return true; }
    if (e.fn && e.code == K_CHAR && tolower(e.ch) == 's') { modified = true; save(); return true; }
    if (k == K_LEFT) { cur = std::max(0, cur - 1); return true; }
    if (k == K_RIGHT) { cur = std::min((int)buf.length(), cur + 1); return true; }
    if (k == K_UP || k == K_DOWN) {
      int col = cur - lines[ln].start;
      int t = ln + (k == K_UP ? -1 : 1);
      if (t >= 0 && t < (int)lines.size()) {
        bool wrapped = t + 1 < (int)lines.size() && lines[t + 1].start == lines[t].start + lines[t].len;
        int maxc = wrapped ? std::max(0, lines[t].len - 1) : lines[t].len;
        cur = lines[t].start + std::min(col, maxc);
      }
      cur = std::min(cur, (int)buf.length());
      return true;
    }
    if (e.code == K_BACK) {
      if (e.fn) { if (cur < (int)buf.length()) { buf.remove(cur, 1); modified = true; } }
      else if (cur > 0) { buf.remove(cur - 1, 1); cur--; modified = true; }
      return true;
    }
    if (e.code == K_ENTER) { buf = buf.substring(0, cur) + "\n" + buf.substring(cur); cur++; modified = true; return true; }
    if (e.code == K_TAB) { buf = buf.substring(0, cur) + "  " + buf.substring(cur); cur += 2; modified = true; return true; }
    if (e.code == K_CHAR && !e.fn && buf.length() < 16000) {
      buf = buf.substring(0, cur) + e.ch + buf.substring(cur);
      cur++; modified = true;
      return true;
    }
    return true;
  }

  void drawList() {
    background();
    statusBar("NOTES", T.green);
    panel(3, 15, 234, 108, T.edge);
    if (!sdOK) textC(120, 60, "INSERT AN SD CARD", T.red);
    else if (files.empty()) { textC(120, 50, "NO NOTES YET", T.white); textC(120, 64, "PRESS N TO CREATE ONE", T.grey); }
    int top = std::max(0, sel - 8);
    for (int r = 0; r < 9 && top + r < (int)files.size(); r++) {
      int i = top + r, y = 19 + r * 11;
      bool s = i == sel;
      if (s) { cv.fillRect(5, y - 2, 230, 11, lerpc(T.panel, T.green, 0.2f)); cv.fillRect(5, y - 2, 2, 11, T.green); }
      textFit(12, y, files[i].c_str(), s ? T.white : T.grey, 210, false);
    }
    hintBar({{"OK", "OPEN"}, {"N", "NEW"}, {"D", "DELETE"}, {"ESC", "BACK"}});
    if (mode == NAME) {
      panel(30, 45, 180, 40, T.green, T.bg1);
      text(36, 50, "NEW NOTE NAME", T.green);
      text(36, 66, (nameIn + ((millis() / 400) % 2 ? "_" : " ")).c_str(), T.white, 1, false);
    }
    if (mode == CONFIRM_DEL) {
      panel(30, 45, 180, 40, T.red, T.bg1);
      text(36, 50, "DELETE NOTE?", T.red);
      textFit(36, 62, files[sel].c_str(), T.white, 168, false);
      text(36, 73, "Y = YES   ANY = NO", T.grey, 1, false);
    }
  }

  void drawEdit() {
    background(true);
    layout();
    String t = baseName(path); t.toUpperCase();
    statusBar(modified ? (t + "*").c_str() : t.c_str(), T.green);
    int ln = curLine();
    if (ln < scroll) scroll = ln;
    if (ln >= scroll + ROWS) scroll = ln - ROWS + 1;
    // which visual lines start a real line (for markdown colours)
    for (int r = 0; r < ROWS && scroll + r < (int)lines.size(); r++) {
      int li = scroll + r;
      VL& v = lines[li];
      int y = 14 + r * 10;
      // find start of the logical line
      int ls = v.start;
      while (ls > 0 && buf[ls - 1] != '\n') ls--;
      uint16_t c = T.white;
      if (buf[ls] == '#') c = T.green;
      else if (buf.startsWith("- ", ls) || buf.startsWith("* ", ls)) c = T.cyan;
      else if (buf.startsWith("> ", ls)) c = T.purp;
      else if (buf.startsWith("TODO", ls) || buf.startsWith("todo", ls)) c = T.yel;
      else if (buf.startsWith("- [x]", ls)) c = T.dim;
      String s = buf.substring(v.start, v.start + v.len);
      s.replace("\n", "");
      text(4, y, s.c_str(), c, 1, false);
      char nb[5]; snprintf(nb, sizeof(nb), "%d", li + 1);
      if (v.start == ls) text(236 - textW(nb), y, nb, T.dim, 1, false);
      if (li == ln && (millis() / 450) % 2 == 0) {
        int cx = 4 + (cur - v.start) * 6;
        cv.fillRect(cx, y - 1, 1, 9, T.green);
      }
    }
    char b[24];
    snprintf(b, sizeof(b), "%d WORDS", words());
    hintBar({{"FN+S", "SAVE"}, {"FN", "+ARROWS"}, {"ESC", "CLOSE"}, {"", b}});
  }
  void draw() override { if (mode == EDIT) drawEdit(); else drawList(); }
};

App* appNotes = new NotesApp();

int notesList(std::vector<String>& out) {
  File d = SD.open("/notes");
  if (!d) return 0;
  while (true) {
    File f = d.openNextFile();
    if (!f) break;
    String n = f.name();
    if (!f.isDirectory() && !n.startsWith(".")) out.push_back(n);
    f.close();
  }
  d.close();
  return out.size();
}
void openNote(const String& name) {
  openApp(appNotes);
  ((NotesApp*)appNotes)->open("/notes/" + name);
}
