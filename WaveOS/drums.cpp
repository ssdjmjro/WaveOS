// WAVE OS - DRUMS: 16-step drum machine, 8 synthesized drums, 4 patterns, presets,
// swing, tempo shared with the looper (and locks to it when the looper is running)
#include "common.h"
#include "audio.h"

struct DrumsApp : App {
  int row = 0, col = 0;
  bool help = false;
  const char* NAMES[8] = {"KICK", "SNARE", "HAT", "OPEN", "CLAP", "TOM", "RIM", "BELL"};
  struct Preset { const char* n; uint16_t p[8]; };
  // bit i = step i (bit 0 = first step)
  static uint16_t B(const char* s) { uint16_t v = 0; for (int i = 0; i < 16 && s[i]; i++) if (s[i] == 'x') v |= 1 << i; return v; }
  std::vector<Preset> presets;
  int preset = -1;
  DrumsApp() {
    name = "DRUMS";
    presets = {
        {"ROCK", {B("x.......x.x....."), B("....x.......x..."), B("x.x.x.x.x.x.x.x."), 0, 0, 0, 0, 0}},
        {"FUNK", {B("x..x..x...x..x.."), B("....x..x.x..x..."), B("xxxxxxxxxxxxxxxx"), B("......x.......x."), 0, 0, B("..x.......x....."), 0}},
        {"HOUSE", {B("x...x...x...x..."), 0, B("..x...x...x...x."), B("..x...x...x...x."), B("....x.......x..."), 0, 0, 0}},
        {"HIPHOP", {B("x......xx.x....."), B("....x.......x..."), B("x.x.x.x.x.x.x.xx"), 0, 0, 0, B(".......x......x."), 0}},
        {"TRAP", {B("x.........x....."), B("........x......."), B("xxxxxxxxxxxxxxxx"), B("...x.......x...."), B("........x......."), 0, 0, 0}},
        {"METAL", {B("xxxxxxxxxxxxxxxx"), B("....x.......x..."), 0, B("x.x.x.x.x.x.x.x."), 0, B("..............xx"), 0, 0}},
        {"REGGAETON", {B("x...x...x...x..."), B("...x..x....x..x."), B("x.x.x.x.x.x.x.x."), 0, 0, 0, B("...x..x....x..x."), 0}},
        {"BOSSA", {B("x..x..x.x..x..x."), 0, B("x.x.x.x.x.x.x.x."), 0, 0, 0, B("x..x..x...x..x.."), 0}},
    };
  }
  void enter() override { static bool loaded = false; if (!loaded) { loaded = true; load(); if (empty()) applyPreset(0); } }
  void leave() override { save(); }
  bool empty() { for (int t = 0; t < 8; t++) if (Drums::pat[t][Drums::cur]) return false; return true; }
  void applyPreset(int i) { preset = i; for (int t = 0; t < 8; t++) Drums::pat[t][Drums::cur] = presets[i].p[t]; }
  void save() {
    if (!sdOK) return;
    ensureDir("/drums");
    File f = SD.open("/drums/patterns.txt", FILE_WRITE);
    if (!f) return;
    f.printf("bpm=%d swing=%d\n", Looper::bpm, Drums::swing);
    for (int p = 0; p < 4; p++) {
      for (int t = 0; t < 8; t++) {
        char l[17];
        for (int i = 0; i < 16; i++) l[i] = Drums::pat[t][p] >> i & 1 ? 'x' : '.';
        l[16] = 0;
        f.printf("%d %d %s\n", p, t, l);
      }
    }
    f.close();
  }
  void load() {
    File f = SD.open("/drums/patterns.txt");
    if (!f) return;
    String h = f.readStringUntil('\n');
    int sw = h.indexOf("swing=");
    if (sw > 0) Drums::swing = constrain((int)h.substring(sw + 6).toInt(), 0, 50);
    while (f.available()) {
      String l = f.readStringUntil('\n');
      int p = l.toInt(), t = l.substring(2).toInt();
      if (p < 0 || p > 3 || t < 0 || t > 7 || l.length() < 20) continue;
      Drums::pat[t][p] = B(l.c_str() + 4);
    }
    f.close();
  }
  bool key(const KeyEv& e) override {
    if (help) { help = false; return true; }
    KeyCode k = e.nav();
    uint16_t& cell = Drums::pat[row][Drums::cur];
    if (k == K_UP) { row = (row + 7) % 8; return true; }
    if (k == K_DOWN) { row = (row + 1) % 8; return true; }
    if (k == K_LEFT) { col = (col + 15) % 16; return true; }
    if (k == K_RIGHT) { col = (col + 1) % 16; return true; }
    if (k == K_ENTER) { cell ^= 1 << col; if (cell >> col & 1) Audio::drumHit(row); col = (col + 1) % 16; return true; }
    if (e.code == K_BACK) { cell = 0; toast("ROW CLEARED", 600); return true; }
    if (e.code != K_CHAR) return false;
    char c = tolower(e.ch);
    char b[32];
    switch (c) {
      case ' ': if (Drums::playing) Drums::stop(); else Drums::play(); return true;
      case '1': case '2': case '3': case '4': {
        int p = c - '1';
        if (Drums::playing) { Drums::queued = p; snprintf(b, sizeof(b), "PATTERN %d NEXT BAR", p + 1); toast(b, 800); }
        else Drums::cur = p;
        return true;
      }
      case 'p': applyPreset((preset + 1) % presets.size()); snprintf(b, sizeof(b), "PRESET %s", presets[preset].n); toast(b, 800); return true;
      case 'm': Drums::mute[row] = !Drums::mute[row]; return true;
      case 'h': Audio::drumHit(row); return true;  // audition
      case 'b': Looper::bpm = std::max(40, Looper::bpm - (e.rep ? 5 : 1)); return true;
      case 'n': Looper::bpm = std::min(240, Looper::bpm + (e.rep ? 5 : 1)); return true;
      case 'w': Drums::swing = (Drums::swing + 10) % 60; snprintf(b, sizeof(b), "SWING %d%%", Drums::swing); toast(b, 600); return true;
      case 'r': {  // random fill for this row (euclidean-ish)
        int hits = random(2, 9), rot = random(16);
        uint16_t v = 0;
        for (int i = 0; i < 16; i++) if ((i * hits) % 16 < hits) v |= 1 << ((i + rot) % 16);
        cell = v; return true;
      }
      case 'c': for (int t = 0; t < 8; t++) Drums::pat[t][Drums::cur] = 0; toast("PATTERN CLEARED", 600); return true;
      case 'd': {  // duplicate current pattern to next slot
        int to = (Drums::cur + 1) % 4;
        for (int t = 0; t < 8; t++) Drums::pat[t][to] = Drums::pat[t][Drums::cur];
        snprintf(b, sizeof(b), "COPIED TO %d", to + 1); toast(b, 700); return true;
      }
      case '\\': case '?': help = true; return true;
      case 'z': case 'x': case 'v': case 'a': case 's': case 'f': case 'g': case 'j': case 'k': case 'l': {
        const char* pads = "zxvasfgjkl";  // finger drumming
        int t = strchr(pads, c) - pads;
        if (t < 8) Audio::drumHit(t);
        return true;
      }
    }
    return false;
  }
  void draw() override {
    background(true);
    statusBar("DRUMS", T.pink);
    char b[32];
    snprintf(b, sizeof(b), "%dBPM P%d", Looper::bpm, Drums::cur + 1);
    text(70, 2, b, T.white, 1, false);
    int gx = 36, gy = 16, cw = 12, ch = 12;
    int playStep = Drums::playing ? Drums::step : -1;
    for (int t = 0; t < 8; t++) {
      int y = gy + t * ch;
      bool sel = t == row;
      text(3, y + 2, NAMES[t], Drums::mute[t] ? T.dim : (sel ? T.white : T.grey), 1, false);
      for (int i = 0; i < 16; i++) {
        int x = gx + i * cw;
        bool on = Drums::pat[t][Drums::cur] >> i & 1;
        uint16_t base = (i / 4) % 2 ? T.bg2 : T.bg1;
        uint16_t c = on ? (Drums::mute[t] ? T.dim : gradc(t / 7.f)) : base;
        if (i == playStep) c = on ? T.white : lerpc(base, T.white, 0.25f);
        cv.fillRect(x, y, cw - 2, ch - 2, c);
        if (on) cv.drawFastHLine(x, y, cw - 2, lerpc(c, T.white, 0.5f));
        if (sel && i == col) frame(x - 1, y - 1, cw, ch, T.yel);
      }
    }
    // step ruler
    for (int i = 0; i < 16; i++) cv.fillRect(gx + i * cw + 3, gy + 8 * ch + 1, 4, 2, i == playStep ? T.pink : (i % 4 ? T.dim : T.grey));
    for (int p = 0; p < 4; p++) {
      uint16_t c = p == Drums::cur ? T.pink : (p == Drums::queued ? T.yel : T.dim);
      snprintf(b, sizeof(b), "%d", p + 1);
      text(gx + 200 - 4, gy + 1 + p * 10, "", c);
      (void)c;
    }
    snprintf(b, sizeof(b), "SW%d %s", Drums::swing, preset >= 0 ? presets[preset].n : "");
    textFit(128, 2, b, T.dim, 36, false);  // keep clear of the volume / battery icons
    hintBar({{"OK", "STEP"}, {"SPC", "PLAY"}, {"P", "PRESET"}, {"1-4", "PATTERN"}, {"\\", "HELP"}});
    if (help)
      helpOverlay("DRUM MACHINE", {"ARROWS  MOVE", "OK  TOGGLE STEP", "SPC  PLAY/STOP", "1-4  PATTERN", "P  PRESET BEAT", "R  RANDOM ROW", "DEL  CLEAR ROW",
                                   "C  CLEAR PATTERN", "D  COPY TO NEXT", "M  MUTE ROW", "H  HEAR SOUND", "B N  TEMPO", "W  SWING", "- =  VOLUME",
                                   "ZXVASFGJKL", "  FINGER DRUMS", "SYNCS WITH LOOPER"});
  }
};

App* appDrums = new DrumsApp();
