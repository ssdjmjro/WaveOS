// =====================================================================
//  WAVE OS  -  custom firmware for the M5Stack Cardputer ADV
//  DAP + synth/looper + IR remote + tools + notes + games
// =====================================================================
#include "common.h"
#include "boot_img.h"
#include "audio.h"
#include <SPI.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <new>

SET_LOOP_TASK_STACK_SIZE(12 * 1024);

M5Canvas cv(&M5Cardputer.Display);
bool sdOK = false;
Settings cfg;
static SPIClass sdSPI(HSPI);
static App* cur = nullptr;

// ------------------------------------------------------------------ util
String fmtTime(uint32_t s) {
  char b[12];
  if (s >= 3600) snprintf(b, sizeof(b), "%u:%02u:%02u", s / 3600, (s / 60) % 60, s % 60);
  else snprintf(b, sizeof(b), "%u:%02u", s / 60, s % 60);
  return String(b);
}
void ensureDir(const char* p) {
  if (sdOK && !SD.exists(p)) SD.mkdir(p);
}
String baseName(const String& p) {
  int i = p.lastIndexOf('/');
  String n = i >= 0 ? p.substring(i + 1) : p;
  int d = n.lastIndexOf('.');
  return d > 0 ? n.substring(0, d) : n;
}
String parentName(const String& p) {
  int i = p.lastIndexOf('/');
  if (i <= 0) return "SD CARD";
  String d = p.substring(0, i);
  int j = d.lastIndexOf('/');
  return j >= 0 ? d.substring(j + 1) : d;
}
bool imuAccel(float& ax, float& ay, float& az) {
  if (!M5.Imu.isEnabled()) return false;
  M5.Imu.update();
  return M5.Imu.getAccel(&ax, &ay, &az);
}

void saveSettings() {
  if (!sdOK) return;
  ensureDir("/waveos");
  File f = SD.open("/waveos/settings.txt", FILE_WRITE);
  if (!f) return;
  f.printf("volume=%d\nmaxvol=%d\nbright=%d\ntheme=%d\nclick=%d\nirrx=%d\nshake=%d\ndim=%d\nsleep=%d\nmidich=%d\nrot=%d\nmusicoff=%d\nmute=%d\nbootsound=%d\n",
           cfg.volume, cfg.maxVol, cfg.bright, cfg.theme, cfg.click, cfg.irRx, cfg.shake, cfg.dimSec, cfg.sleepMin, cfg.midiCh, cfg.rot, cfg.musicOff, cfg.mute, cfg.bootSound);
  f.close();
}
void loadSettings() {
  if (!sdOK) return;
  File f = SD.open("/waveos/settings.txt");
  if (!f) return;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    int e = l.indexOf('=');
    if (e < 0) continue;
    String k = l.substring(0, e);
    int v = l.substring(e + 1).toInt();
    if (k == "volume") cfg.volume = constrain(v, 0, 255);
    else if (k == "maxvol") cfg.maxVol = constrain(v, 30, 255);
    else if (k == "bright") cfg.bright = constrain(v, 10, 255);
    else if (k == "theme") cfg.theme = constrain(v, 0, 2);
    else if (k == "mono") cfg.theme = v ? 1 : 0;
    else if (k == "click") cfg.click = v;
    else if (k == "irrx") cfg.irRx = (v == 2) ? 2 : 1;
    else if (k == "shake") cfg.shake = v;
    else if (k == "dim") cfg.dimSec = constrain(v, 0, 250);
    else if (k == "sleep") cfg.sleepMin = constrain(v, 0, 120);
    else if (k == "midich") cfg.midiCh = constrain(v, 1, 16);
    else if (k == "rot") cfg.rot = (v == 1) ? 1 : 3;
    else if (k == "musicoff") cfg.musicOff = constrain(v, 0, 250);
    else if (k == "mute") cfg.mute = v;
    else if (k == "bootsound") cfg.bootSound = v;
  }
  f.close();
  cfg.volume = std::min(cfg.volume, cfg.maxVol);
  cfg.mono = cfg.theme == 1;
}

// ------------------------------------------------------------------ display orientation
// always landscape (240 wide): NORMAL = first landscape rotation, FLIPPED = the other one
void applyRotation() {
  int land[2] = {1, 3}, n = 0;
  for (int r = 0; r < 4 && n < 2; r++) {
    M5Cardputer.Display.setRotation(r);
    if (M5Cardputer.Display.width() > M5Cardputer.Display.height()) land[n++] = r;
  }
  M5Cardputer.Display.setRotation(cfg.rot == 3 ? land[0] : land[1]);
  Serial.printf("[waveos] using rotation %d (%dx%d)\n", M5Cardputer.Display.getRotation(), (int)M5Cardputer.Display.width(), (int)M5Cardputer.Display.height());
}

void memLog(const char* where) {
  Serial.printf("[mem] %s free=%u maxblock=%u minfree=%u app=%s\n", where, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
                (unsigned)ESP.getMinFreeHeap(), cur ? cur->name : "-");
  if (!sdOK) return;
  File f = SD.open("/waveos/mem.txt", FILE_APPEND);
  if (f) { f.printf("%s v%s free=%u maxblock=%u minfree=%u\n", where, FW_VER, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(), (unsigned)ESP.getMinFreeHeap()); f.close(); }
}

// ------------------------------------------------------------------ app switching
void openApp(App* a) {
  if (!a || a == cur) return;
  if (cur) cur->leave();
  cur = a;
  try { cur->enter(); }
  catch (std::bad_alloc&) { char b[40]; snprintf(b, sizeof(b), "OUT OF MEMORY (%uK)", (unsigned)(ESP.getMaxAllocHeap() / 1024)); toast(b, 2500); memLog("oom-open"); cur = appHome; }
}
void goBack() { openApp(cur && cur->parent ? cur->parent : appHome); }
void goHome() { openApp(appHome); }

// ===================================================================== HOME (+ global search)
struct HomeApp : App {
  struct Item { const char* n; int icon; App** target; };
  static constexpr int N = 9;
  Item items[N] = {{"MUSIC", IC_MUSIC, &appMusic}, {"SYNTH", IC_SYNTH, &appSynth}, {"DRUMS", IC_DRUMS, &appDrums},
                   {"IR", IC_IR, &appIR},
                   {"TOOLS", IC_TOOLS, &appTools}, {"NOTES", IC_NOTES, &appNotes}, {"GAMES", IC_GAMES, &appGames},
                   {"FUN", IC_FUN, &appFun}, {"SETUP", IC_SETUP, &appSettings}};
  int sel = 0;
  float anim = 0, phase = 0;
  // search
  bool searching = false;
  String q;
  struct Res { int type; int idx; String label; };  // 0 app, 1 song, 2 note
  std::vector<Res> res;
  int rsel = 0;
  HomeApp() { name = "HOME"; }
  bool wantsAllKeys() override { return searching; }
  uint16_t acc(int i) {
    switch (i) {
      case 0: return T.cyan; case 1: return T.purp; case 2: return T.pink;
      case 3: return T.red; case 4: return T.yel; case 5: return T.green; case 6: return T.blue; case 7: return T.green; default: return T.grey;
    }
  }
  const char* sub(int i) {
    static char b[40];
    switch (i) {
      case 0:
        if (Music::active()) { snprintf(b, sizeof(b), "%s %s", Music::paused() ? "PAUSED" : "PLAYING", fmtTime((uint32_t)Music::pos()).c_str()); return b; }
        return "WAV MP3 FLAC AAC";
      case 1: return Looper::playing ? "LOOPER RUNNING" : "ARP CHORDS LOOPER";
      case 2: return Drums::playing ? "BEAT RUNNING" : "16 STEP DRUM MACHINE";
      case 3: return "TV  LEARN  RECEIVE  .IR";
      case 4: return "CALC METRONOME TIMER";
      case 5: return "MARKDOWN ON SD";
      case 6: return "17 GAMES";
      case 7: return "13 QUIET GAMES + TOYS";
      default: return "SOUND SCREEN THEME";
    }
  }
  void runSearch() {
    res.clear(); rsel = 0;
    if (!q.length()) return;
    String lq = q; lq.toLowerCase();
    for (int i = 0; i < N; i++) { String n = items[i].n; n.toLowerCase(); if (n.indexOf(lq) >= 0) res.push_back({0, i, String("APP  ") + items[i].n}); }
    const char* extra[][2] = {{"CALC", "c"}, {"GRAPH", "g"}, {"RESISTOR", "r"}, {"ELEC", "e"}, {"TIMER", "t"}, {"SNAKE", "s"}, {"MERGE", "2"},
                              {"BLOCKS", "b"}, {"BRICKS", "o"}, {"GLIDER", "f"}, {"TYPING", "t"}, {"RHYTHM", "y"},
                              {"STARFALL", "x"}, {"MINES", "m"}, {"DUEL", "d"}, {"ECHO", "h"}, {"RUNNER", "u"},
                              {"MAZE", "z"}, {"FOUR", "4"}, {"TANKS", "k"}, {"ROCKS", "q"}, {"HOPPER", "p"},
                              {"SUDOKU", "1"}, {"WORDS", "2"}, {"PET", "3"}, {"DOODLE", "5"}, {"TIC TAC TOE", "6"}, {"BUBBLES", "7"}, {"ORACLE", "8"},
                              {"FLASHLIGHT", "l"}, {"MORSE", "o"}, {"FIVE", ""}, {"CODE", ""}, {"LIGHTS", ""}, {"SLIDE", ""},
                              {"REFLEX", ""}, {"LIFE", ""}, {"METRONOME", ""}};
    for (int i = 0; i < 31; i++) { String n = extra[i][0]; n.toLowerCase(); if (n.indexOf(lq) >= 0) res.push_back({3, i, String("APP  ") + extra[i][0]}); }
    std::vector<int> songs;
    searchSongs(q, songs, 6);
    for (int s : songs) res.push_back({1, s, "SONG " + baseName(songPath(s))});
    std::vector<String> notes;
    notesList(notes);
    for (auto& n : notes) { String l = n; l.toLowerCase(); if (l.indexOf(lq) >= 0 && res.size() < 14) res.push_back({2, 0, "NOTE " + n}); }
  }
  void openRes(const Res& r) {
    searching = false;
    if (r.type == 0) openApp(*items[r.idx].target);
    else if (r.type == 1) playSong(r.idx);
    else if (r.type == 2) openNote(r.label.substring(5));
    else {
      App* sub[] = {appCalc, appGraph, appResistor, appElec, appTimer, appSnake, app2048, appBlocks, appBreakout, appFlappy, appTyping, appRhythm,
                    appStarfall, appMines, appDuel, appEcho, appRunner, appMaze, appFour, appTanks, appRocks, appHopper,
                    appSudoku, appWords, appPet, appDoodle, appTtt, appBubbles, appOracle, appFlash, appMorse,
                    appFive, appCode, appLights, appSlide, appReflex, appLife, appMetro};
      openApp(sub[r.idx]);
    }
  }
  bool key(const KeyEv& e) override {
    if (searching) {
      KeyCode k = e.navFn();
      if (e.code == K_ESC) { searching = false; return true; }
      if (e.code == K_BACK) { if (q.length()) q.remove(q.length() - 1); if (!q.length()) searching = false; runSearch(); return true; }
      if (k == K_UP && !res.empty()) { rsel = (rsel + res.size() - 1) % res.size(); return true; }
      if (k == K_DOWN && !res.empty()) { rsel = (rsel + 1) % res.size(); return true; }
      if (e.code == K_ENTER && !res.empty()) { openRes(res[rsel]); return true; }
      if (e.code == K_CHAR && !e.fn && e.ch >= 32 && q.length() < 20) { q += e.ch; runSearch(); }
      return true;
    }
    KeyCode k = e.nav();
    if (k == K_LEFT || k == K_UP) { sel = (sel + N - 1) % N; return true; }
    if (k == K_RIGHT || k == K_DOWN) { sel = (sel + 1) % N; return true; }
    if (k == K_ENTER) { openApp(*items[sel].target); return true; }
    if (e.code == K_CHAR && isdigit((unsigned char)e.ch)) { if (e.ch == '0') return true; sel = e.ch - '1'; openApp(*items[sel].target); return true; }  // 1-9
    if (e.code == K_CHAR && isalpha((unsigned char)e.ch) && !e.fn) { searching = true; q = String(e.ch); runSearch(); return true; }
    if (k == K_ESC) return true;
    return false;
  }
  void draw() override {
    float d = sel - anim;
    if (d > N / 2.f) anim += N;
    if (d < -N / 2.f) anim -= N;
    anim += (sel - anim) * 0.35f;
    if (fabsf(sel - anim) < 0.01f) anim = sel;
    phase += 0.03f;
    background();
    statusBar("HOME", T.cyan);
    int cx = 120;
    for (int k = -3; k <= 3; k++) {
      int i = ((int)roundf(anim) + k + N * 10) % N;
      float rel = ((int)roundf(anim) + k) - anim;
      int x = cx + (int)(rel * 58);
      float ad = fabsf(rel);
      if (ad < 0.5f || x < -20 || x > 260) continue;
      uint16_t c = lerpc(acc(i), T.bg1, ad < 1.5f ? 0.45f : 0.7f);
      panel(x - 14, 34, 28, 28, lerpc(T.edge, T.bg0, ad < 1.5f ? 0.3f : 0.6f));
      drawIcon(x - 8, 40, items[i].icon, 1, c);
    }
    int ci = ((int)roundf(anim) + N * 10) % N;
    float rel = roundf(anim) - anim;
    int x = cx + (int)(rel * 58);
    uint16_t c = acc(ci);
    panel(x - 26, 22, 52, 52, c, lerpc(T.panel, c, 0.08f));
    frame(x - 27, 21, 54, 54, lerpc(T.bg0, c, 0.35f));
    drawIcon(x - 16, 32, items[ci].icon, 2, c);
    textC(cx, 80, items[sel].n, T.white, 2);
    textC(cx, 97, sub(sel), T.grey);
    for (int i = 0; i < N; i++) {
      int dx = cx - N * 4 + i * 8;
      cv.fillRect(dx, 108, i == sel ? 4 : 2, 2, i == sel ? T.cyan : T.dim);
    }
    hintBar({{"<>", "MOVE"}, {"OK", "OPEN"}, {"1-9", "JUMP"}, {"A-Z", "SEARCH"}});
    if (searching) {
      panel(14, 16, 212, 106, T.cyan, T.bg1);
      frame(13, 15, 214, 108, lerpc(T.bg0, T.cyan, 0.3f));
      text(20, 21, "SEARCH:", T.cyan);
      text(70, 21, (q + ((millis() / 400) % 2 ? "_" : "")).c_str(), T.white);
      if (res.empty()) textC(120, 60, "NO RESULTS", T.dim);
      int top = std::max(0, rsel - 7);
      for (int r = 0; r < 8 && top + r < (int)res.size(); r++) {
        int i = top + r, y = 34 + r * 11;
        bool s = i == rsel;
        if (s) cv.fillRect(16, y - 2, 208, 11, lerpc(T.panel, T.cyan, 0.25f));
        uint16_t tc = res[i].type == 1 ? T.cyan : (res[i].type == 2 ? T.green : T.yel);
        text(20, y, res[i].label.substring(0, 4).c_str(), tc, 1, false);
        textFit(52, y, res[i].label.substring(5).c_str(), s ? T.white : T.grey, 168, false);
      }
    }
  }
};

// ===================================================================== card menu (Tools / Games), scrolls
struct MenuApp : App {
  struct Item { const char* n; const char* d; int icon; App** target; };
  std::vector<Item> items;
  uint16_t (*accent)();
  int sel = 0;
  float anim = 0;
  MenuApp(const char* nm, std::vector<Item> it, uint16_t (*a)()) : items(it), accent(a) { name = nm; }
  bool key(const KeyEv& e) override {
    KeyCode k = e.nav();
    int n = items.size();
    if (k == K_LEFT || k == K_UP) { sel = (sel + n - 1) % n; return true; }
    if (k == K_RIGHT || k == K_DOWN) { sel = (sel + 1) % n; return true; }
    if (k == K_ENTER) { openApp(*items[sel].target); return true; }
    if (e.code == K_CHAR && e.ch >= '1' && e.ch < '1' + n) { sel = e.ch - '1'; openApp(*items[sel].target); return true; }
    return false;
  }
  void draw() override {
    background();
    uint16_t a = accent();
    statusBar(name, a);
    int n = items.size();
    int w = 70, step = 78;
    float target = n <= 3 ? (n - 1) / 2.f : constrain((float)sel, 1.f, n - 2.f);
    anim += (target - anim) * 0.35f;
    for (int i = 0; i < n; i++) {
      int x = 120 - w / 2 + (int)((i - anim) * step), y = 22;
      if (x < -w || x > 240) continue;
      bool s = i == sel;
      if (s) { panel(x, y, w, 76, a, lerpc(T.panel, a, 0.1f)); frame(x - 1, y - 1, w + 2, 78, lerpc(T.bg0, a, 0.35f)); }
      else panel(x, y, w, 76, T.edge);
      drawIcon(x + w / 2 - 16, y + 10, items[i].icon, 2, s ? a : lerpc(a, T.bg1, 0.45f));
      textC(x + w / 2, y + 50, items[i].n, s ? T.white : T.grey);
      textC(x + w / 2, y + 62, items[i].d, s ? a : T.dim, 1, false);
    }
    for (int i = 0; i < n && n > 3; i++) cv.fillRect(120 - n * 4 + i * 8, 104, i == sel ? 4 : 2, 2, i == sel ? a : T.dim);
    hintBar({{"<>", "MOVE"}, {"OK", "OPEN"}, {"ESC", "BACK"}});
  }
};
static uint16_t accYel() { return T.yel; }
static uint16_t accBlue() { return T.blue; }
static uint16_t accGreen() { return T.green; }

// ===================================================================== SETTINGS
struct SettingsApp : App {
  int sel = 0, top = 0;
  static constexpr int N = 14;
  SettingsApp() { name = "SETUP"; }
  void leave() override { saveSettings(); }
  void adjust(int d) {
    switch (sel) {
      case 0: cfg.volume = constrain(cfg.volume + d * 15, 0, (int)cfg.maxVol); cfg.mute = false; applyVolume(); Audio::blip(880, 60); break;
      case 1: cfg.maxVol = constrain(cfg.maxVol + d * 15, 30, 255); cfg.volume = std::min(cfg.volume, cfg.maxVol); applyVolume(); break;
      case 2: cfg.bright = constrain(cfg.bright + d * 20, 10, 255); M5Cardputer.Display.setBrightness(cfg.bright); break;
      case 3: {
        int nt = customThemeExists() ? 3 : 2;
        cfg.theme = (cfg.theme + nt + d) % nt; cfg.mono = cfg.theme == 1; applyTheme(cfg.theme); break;
      }
      case 4: cfg.click = !cfg.click; break;
      case 5: cfg.shake = !cfg.shake; break;
      case 6: { const uint8_t o[] = {0, 30, 60, 120, 240}; int i = 0; while (i < 5 && o[i] != cfg.dimSec) i++; cfg.dimSec = o[(i + 5 + d) % 5]; break; }
      case 7: { const uint8_t o[] = {0, 5, 10, 30, 60}; int i = 0; while (i < 5 && o[i] != cfg.sleepMin) i++; cfg.sleepMin = o[(i + 5 + d) % 5]; break; }
      case 8: cfg.irRx = cfg.irRx == 1 ? 2 : 1; break;
      case 9: cfg.rot = cfg.rot == 1 ? 3 : 1; applyRotation(); break;
      case 10: { const uint8_t o[] = {0, 10, 20, 45, 90}; int i = 0; while (i < 5 && o[i] != cfg.musicOff) i++; cfg.musicOff = o[(i + 5 + d) % 5]; break; }
      case 11: cfg.bootSound = !cfg.bootSound; break;
    }
  }
  bool key(const KeyEv& e) override {
    KeyCode k = e.nav();
    if (k == K_UP) { sel = (sel + N - 1) % N; return true; }
    if (k == K_DOWN) { sel = (sel + 1) % N; return true; }
    if (k == K_LEFT) { adjust(-1); return true; }
    if (k == K_RIGHT || k == K_ENTER) { adjust(1); return true; }
    return false;
  }
  void bar(int x, int y, int w, float v) {
    cv.fillRect(x, y, w, 3, T.dim);
    for (int i = 0; i < (int)(w * v); i++) cv.drawFastVLine(x + i, y, 3, gradc(i / (float)w));
  }
  void draw() override {
    background();
    statusBar("SETUP", T.grey);
    const char* names[N] = {"VOLUME", "MAX VOLUME", "BRIGHTNESS", "THEME", "KEY CLICK", "SHAKE SHUFFLE", "SCREENSAVER", "AUTO SLEEP", "IR RX PIN", "SCREEN FLIP", "MUSIC SCR OFF", "BOOT SOUND", "BATTERY", "ABOUT"};
    panel(4, 15, 232, 108, T.edge);
    const int rows = 8;
    if (sel < top) top = sel;
    if (sel >= top + rows) top = sel - rows + 1;
    for (int r = 0; r < rows; r++) {
      int i = top + r, y = 19 + r * 13;
      bool s = i == sel;
      if (s) { cv.fillRect(6, y - 2, 228, 12, lerpc(T.panel, T.cyan, 0.15f)); cv.fillRect(6, y - 2, 2, 12, T.cyan); }
      text(12, y, names[i], s ? T.white : T.grey, 1, false);
      char b[40] = "";
      bool hasBar = false;
      switch (i) {
        case 0: hasBar = true; bar(110, y + 2, 80, cfg.volume / 255.f); snprintf(b, sizeof(b), "%d%%", cfg.volume * 100 / 255); break;
        case 1: hasBar = true; bar(110, y + 2, 80, cfg.maxVol / 255.f); snprintf(b, sizeof(b), "%d%%", cfg.maxVol * 100 / 255); break;
        case 2: hasBar = true; bar(110, y + 2, 80, cfg.bright / 255.f); snprintf(b, sizeof(b), "%d%%", cfg.bright * 100 / 255); break;
        case 3: snprintf(b, sizeof(b), "< %s >", cfg.theme == 0 ? "NEON" : (cfg.theme == 1 ? "MONO" : "CUSTOM")); break;
        case 4: snprintf(b, sizeof(b), "%s", cfg.click ? "ON" : "OFF"); break;
        case 5: snprintf(b, sizeof(b), "%s", cfg.shake ? "ON" : "OFF"); break;
        case 6: if (cfg.dimSec) snprintf(b, sizeof(b), "%dS", cfg.dimSec); else snprintf(b, sizeof(b), "NEVER"); break;
        case 7: if (cfg.sleepMin) snprintf(b, sizeof(b), "%d MIN", cfg.sleepMin); else snprintf(b, sizeof(b), "NEVER"); break;
        case 8: snprintf(b, sizeof(b), "GROVE G%d", cfg.irRx); break;
        case 9: snprintf(b, sizeof(b), "%s", cfg.rot == 3 ? "NORMAL" : "FLIPPED"); break;
        case 10: if (cfg.musicOff) snprintf(b, sizeof(b), "%dS", cfg.musicOff); else snprintf(b, sizeof(b), "NEVER"); break;
        case 11: snprintf(b, sizeof(b), "%s", cfg.bootSound ? "ON" : "OFF"); break;
        case 12: snprintf(b, sizeof(b), "%d%%  %.2fV %s", Batt::pct, Batt::mv / 1000.f, Batt::charging ? "CHARGING" : Batt::usb ? "FULL" : "ON BATTERY"); break;
        case 13: snprintf(b, sizeof(b), "%s V%s", FW_NAME, FW_VER); break;
      }
      text(hasBar ? 196 : 120, y, b, s ? T.cyan : T.grey, 1, false);
    }
    if (sel == N - 1) {
      char info[64];
      cv.fillRect(6, 98, 228, 23, T.panel);
      uint64_t total = sdOK ? SD.totalBytes() / (1024 * 1024) : 0, used = sdOK ? SD.usedBytes() / (1024 * 1024) : 0;
      snprintf(info, sizeof(info), "SD %u/%uMB HEAP %uK/%uK %dMHZ", (unsigned)used, (unsigned)total, (unsigned)(ESP.getFreeHeap() / 1024),
               (unsigned)(ESP.getMaxAllocHeap() / 1024), getCpuFrequencyMhz());
      text(12, 100, info, T.dim, 1, false);
      snprintf(info, sizeof(info), "BAT %d%% %.2fV%s %s IMU %s", Batt::pct, Batt::mv / 1000.f, Batt::usb ? "+USB" : "",
               M5.getBoard() == m5::board_t::board_M5CardputerADV ? "ADV" : "CP", M5.Imu.isEnabled() ? "OK" : "--");
      text(12, 110, info, T.dim, 1, false);
    }
    hintBar({{"^v", "SELECT"}, {"<>", "CHANGE"}, {"ESC", "BACK"}});
  }
};

App *appHome, *appTools, *appGames, *appSettings, *appFun;

// ------------------------------------------------------------------ screenshot (FN+P) -> /screenshots/shotN.bmp
static void screenshot() {
  if (!sdOK) { toast("NO SD CARD"); return; }
  ensureDir("/screenshots");
  char p[40];
  int n = 1;
  do { snprintf(p, sizeof(p), "/screenshots/shot%03d.bmp", n++); } while (SD.exists(p) && n < 1000);
  File f = SD.open(p, FILE_WRITE);
  if (!f) { toast("SCREENSHOT FAILED"); return; }
  const int W = 240, H = 135, rowB = W * 3, sz = 54 + rowB * H;
  uint8_t h[54] = {'B', 'M'};
  auto put32 = [&](int o, uint32_t v) { h[o] = v; h[o + 1] = v >> 8; h[o + 2] = v >> 16; h[o + 3] = v >> 24; };
  put32(2, sz); put32(10, 54); put32(14, 40); put32(18, W); put32(22, H);
  h[26] = 1; h[28] = 24; put32(34, rowB * H);
  f.write(h, 54);
  uint8_t row[W * 3];
  for (int y = H - 1; y >= 0; y--) {
    for (int x = 0; x < W; x++) {
      uint16_t c = cv.readPixel(x, y);
      row[x * 3 + 2] = ((c >> 11) & 31) * 255 / 31;
      row[x * 3 + 1] = ((c >> 5) & 63) * 255 / 63;
      row[x * 3] = (c & 31) * 255 / 31;
    }
    f.write(row, rowB);
  }
  f.close();
  Audio::blip(2400, 40, Audio::W_SINE, 0.3f);
  toast(p + 13);
}

// ------------------------------------------------------------------ boot
static void drawBootFrame(float t) {
  // the neon wave picture, revealed left -> right behind a soft edge
  const uint16_t BG = RGB(2, 4, 10);
  cv.drawJpg(BOOT_JPG, sizeof(BOOT_JPG), 0, 0);
  int rx = (int)(std::min(t * 1.15f, 1.f) * 270) - 20;
  for (int x = std::max(0, rx); x < 240; x++) {
    float k = constrain((x - rx) / 18.f, 0.f, 1.f);
    if (k >= 1) { cv.fillRect(x, 0, 240 - x, 135, BG); break; }
    for (int y = 0; y < 135; y++) cv.drawPixel(x, y, lerpc(cv.readPixel(x, y), BG, k));
  }
  float a = constrain((t - 0.5f) * 2.f, 0.f, 1.f);  // fade the title in
  if (a > 0) {
    textC(120, 100, FW_NAME, lerpc(BG, RGB(235, 242, 255), a), 2, false);
    textC(120, 119, "CARDPUTER ADV  V" FW_VER, lerpc(BG, RGB(110, 124, 150), a), 1, false);
  }
}
void libScanStart();
static void bootAnim() {
  // Smooth version: the picture is decoded ONCE into the frame buffer; each frame only sends the
  // newly revealed columns (and later only the title rows) to the screen, instead of re-decoding
  // and re-sending the whole 240x135 image every frame.
  const uint8_t chime[4] = {72, 76, 79, 84};
  const uint16_t BG = RGB(2, 4, 10);
  auto& D = M5Cardputer.Display;
  cv.drawJpg(BOOT_JPG, sizeof(BOOT_JPG), 0, 0);
  D.fillScreen(BG);
  uint32_t t0 = millis();
  int ni = 0, shown = 0;
  const uint32_t REVEAL = 900, DUR = 1650;
  while (millis() - t0 < DUR) {
    uint32_t el = millis() - t0;
    int rx = std::min(240, (int)(240 * el / REVEAL));
    if (rx > shown) {  // push only the new strip
      D.setClipRect(shown, 0, rx - shown, 135);
      cv.pushSprite(0, 0);
      D.clearClipRect();
      shown = rx;
    }
    float a = constrain((el - 550.f) / 450.f, 0.f, 1.f);  // title fades in (drawn over itself, getting brighter)
    if (a > 0) {
      textC(120, 100, FW_NAME, lerpc(BG, RGB(235, 242, 255), a), 2, false);
      textC(120, 119, "CARDPUTER ADV  V" FW_VER, lerpc(BG, RGB(110, 124, 150), a), 1, false);
      int x0 = std::min(shown, 240);
      if (x0 > 0) { D.setClipRect(0, 98, x0, 30); cv.pushSprite(0, 0); D.clearClipRect(); }
    }
    if (cfg.bootSound && ni < 4 && el > 200 + ni * 130) { Audio::blip(440.f * powf(2.f, (chime[ni] - 69) / 12.f), 110, Audio::W_TRI, 0.35f); ni++; }
    delay(12);
  }
  D.waitDisplay();  // let the last frame finish before anything else touches the bus
  // fade the backlight out: the boot -> home switch then happens in the dark (no tearing flash)
  for (int i = 8; i >= 0; i--) { D.setBrightness(cfg.bright * i / 8); delay(18); }
}

// ------------------------------------------------------------------ screensaver / sleep
static uint32_t lastInput = 0;
static bool saver = false, screenOff = false;
static void drawSaver() {
  static float ph = 0;
  ph += 0.02f;
  cv.fillSprite(T.bg0);
  drawWave(10, 230, 60 + (int)(8 * sinf(ph * 0.7f)), 22, ph, true);
  if (Music::active()) {
    char b[40];
    snprintf(b, sizeof(b), "%s  %s", Music::paused() ? "=" : ">", fmtTime((uint32_t)Music::pos()).c_str());
    textC(120, 104, b, T.grey);
  }
  char b[16];
  snprintf(b, sizeof(b), "%d%%", Batt::pct);
  text(206, 4, b, T.dim, 1, false);
}
static void deepSleep() {
  saveSettings();
  cv.fillSprite(T.bg0);
  textC(120, 60, "SLEEPING - PRESS G0", T.grey);
  cv.pushSprite(0, 0);
  delay(800);
  M5Cardputer.Display.setBrightness(0);
  M5Cardputer.Display.sleep();
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
  esp_deep_sleep_start();
}

// ------------------------------------------------------------------ volume overlay + power management
static uint32_t volT = 0;
void applyVolume() { Audio::setVolume(cfg.mute ? 0 : cfg.volume); }
void volumeChange(int d) {
  cfg.volume = constrain((int)cfg.volume + d * 12, 0, (int)cfg.maxVol);
  if (cfg.mute) cfg.mute = false;  // changing volume un-mutes
  applyVolume();
  volT = millis();
}
static uint32_t briT = 0;
static void brightnessChange(int d) {
  cfg.bright = constrain((int)cfg.bright + d * 25, 10, 255);
  M5Cardputer.Display.setBrightness(cfg.bright);
  briT = millis();
}
static void drawBrightness() {
  if (millis() - briT > 1200) return;
  panel(60, 46, 120, 34, T.yel, T.bg1);
  char b[20]; snprintf(b, sizeof(b), "BRIGHTNESS %d%%", cfg.bright * 100 / 255);
  textC(120, 51, b, T.white);
  cv.fillRect(70, 66, 100, 6, T.dim);
  cv.fillRect(70, 66, 100 * cfg.bright / 255, 6, T.yel);
}
static void drawVolume() {
  drawBrightness();
  if (millis() - volT > 1200) return;
  panel(60, 46, 120, 34, T.cyan, T.bg1);
  char b[16]; snprintf(b, sizeof(b), "VOLUME %d%%", cfg.volume * 100 / 255);
  textC(120, 51, b, T.white);
  int w = 100 * cfg.volume / 255, wm = 100 * cfg.maxVol / 255;
  cv.fillRect(70, 66, 100, 6, T.dim);
  for (int i = 0; i < w; i++) cv.drawFastVLine(70 + i, 66, 6, gradc(i / 100.f));
  if (wm < 100) cv.drawFastVLine(70 + wm, 64, 10, T.red);
}
static Settings savedCfg;
static uint32_t cfgChangedT = 0;
static void settingsAutosave() {  // save ~2s after any setting (e.g. volume) changes
  if (memcmp(&savedCfg, &cfg, sizeof(cfg)) == 0) { cfgChangedT = 0; return; }
  if (!cfgChangedT) { cfgChangedT = millis(); return; }
  if (millis() - cfgChangedT > 2000) { saveSettings(); savedCfg = cfg; cfgChangedT = 0; }
}
static void lowBatteryTick() {
  static bool warned10 = false, warned5 = false;
  Batt::tick();
  if (Batt::usb || Batt::pct < 0) { warned10 = warned5 = false; return; }
  if (Batt::pct <= 10 && !warned10) { warned10 = true; toast("BATTERY LOW 10%", 3000); Audio::blip(440, 120, Audio::W_SQUARE, 0.3f); }
  if (Batt::pct <= 4 && !warned5) { warned5 = true; toast("BATTERY CRITICAL - SAVING", 3000); }
}
static int cpuMHz = 160;
static uint32_t lowBlockedUntil = 0, lastUnder = 0;
static void powerTick(bool screenIsOff) {
  bool musicOnly = Music::active() && !Music::paused() && !Looper::playing && !Drums::playing && !MidiPlay::playing;
  uint32_t u = Music::underrunCount();
  if (u != lastUnder) {  // decoder struggled: go back to full speed for a while
    lastUnder = u;
    if (cpuMHz < 160) lowBlockedUntil = millis() + 120000;
  }
  int want = (screenIsOff && musicOnly && millis() > lowBlockedUntil) ? 80 : 160;
  if (want != cpuMHz) { M5Cardputer.Display.waitDisplay(); setCpuFrequencyMhz(want); cpuMHz = want; }
}

extern void musicTick();
extern void timerTick();
extern void irTick();

void setup() {
  setCpuFrequencyMhz(160);  // set BEFORE the display starts: changing it mid-transfer corrupts a row of pixels
  auto c = M5.config();
  M5Cardputer.begin(c, true);
  M5Cardputer.Display.setRotation(1);
  // 40MHz is at the edge for this panel/wiring: run the display bus at 27MHz (still ~30fps full screen)
  if (auto* pd = (lgfx::Panel_Device*)M5Cardputer.Display.getPanel()) if (pd->getBus()) pd->getBus()->setClock(27000000);
  cv.setColorDepth(16);
  cv.createSprite(240, 135);
  sdSPI.begin(40, 39, 14, 12);
  sdOK = SD.begin(12, sdSPI, 25000000);
  loadSettings();
  Serial.begin(115200);
  for (int r = 0; r < 4; r++) {
    M5Cardputer.Display.setRotation(r);
    Serial.printf("[waveos] board=%d rot=%d w=%d h=%d\n", (int)M5.getBoard(), r, (int)M5Cardputer.Display.width(), (int)M5Cardputer.Display.height());
  }
  applyRotation();
  M5Cardputer.Display.setBrightness(cfg.bright);
  applyTheme(cfg.theme);
  if (sdOK) {
    const char* dirs[] = {"/waveos", "/notes", "/music", "/loops", "/ir", "/playlists", "/themes", "/screenshots"};
    for (auto d : dirs) ensureDir(d);
    if (!SD.exists("/themes/custom.txt")) {
      File f = SD.open("/themes/example.txt", FILE_WRITE);
      if (f) {
        f.println("# copy to custom.txt and pick CUSTOM theme in SETUP");
        f.println("bg0=040710\nbg1=0A1020\nbg2=142038\npanel=0C1424\nedge=2A3A60\ncyan=FF8A3D\nblue=FF5577\npurple=B04CFF");
        f.println("pink=FF5CBE\nwhite=F0F0FF\ngrey=8090A0\ndim=343E54\ngreen=5AF096\nyellow=FFD25A\nred=FF5064\nbar=060A14");
        f.close();
      }
    }
  }
  Audio::begin();
  Audio::setVolume(cfg.mute ? 0 : cfg.volume);

  savedCfg = cfg;
  appHome = new HomeApp();
  appTools = new MenuApp("TOOLS", {{"CALC", "SCIENTIFIC", IC_CALC, &appCalc}, {"GRAPH", "F(X) PLOT", IC_GRAPH, &appGraph},
                                   {"RESISTOR", "COLOR CODE", IC_RES, &appResistor}, {"ELEC", "OHM LED 555", IC_ELEC, &appElec},
                                   {"TIMER", "STOPWATCH", IC_TIMER, &appTimer}, {"METRONOME", "TAP TEMPO", IC_METRO, &appMetro},
                                   {"FLASHLIGHT", "LIGHT / SOS", IC_FLASH, &appFlash}, {"MORSE", "BEEP CODE", IC_MORSE, &appMorse}}, accYel);
  appGames = new MenuApp("GAMES", {{"STARFALL", "SHOOTER", IC_STARFALL, &appStarfall}, {"RUNNER", "ENDLESS", IC_RUNNER, &appRunner},
                                   {"SNAKE", "CLASSIC", IC_SNAKE, &appSnake}, {"BLOCKS", "LINE CLEAR", IC_BLOCKS, &appBlocks},
                                   {"MERGE", "NUMBER PUZZLE", IC_2048, &app2048}, {"MINES", "LOGIC", IC_MINES, &appMines},
                                   {"BRICKS", "KEYS / TILT", IC_BREAKOUT, &appBreakout}, {"DUEL", "VS CPU", IC_DUEL, &appDuel},
                                   {"GLIDER", "ONE BUTTON", IC_FLAPPY, &appFlappy}, {"ECHO", "MEMORY", IC_ECHO, &appEcho},
                                   {"TYPING", "WORD RAIN", IC_TYPING, &appTyping}, {"RHYTHM", "D F J K", IC_RHYTHM, &appRhythm},
                                   {"ROCKS", "SPACE ROCKS", IC_ROCKS, &appRocks}, {"TANKS", "ARTILLERY", IC_TANKS, &appTanks},
                                   {"FOUR", "VS CPU", IC_FOUR, &appFour}, {"HOPPER", "CROSS IT", IC_HOPPER, &appHopper},
                                   {"MAZE", "ESCAPE", IC_MAZE, &appMaze}}, accBlue);
  appSettings = new SettingsApp();
  appFun = new MenuApp("FUN", {{"SUDOKU", "LOGIC", IC_SUDOKU, &appSudoku}, {"WORDS", "GUESS IT", IC_WORDS, &appWords},
                               {"PIXEL PET", "CARE FOR IT", IC_PET, &appPet}, {"DOODLE", "PIXEL ART", IC_DOODLE, &appDoodle},
                               {"TIC TAC TOE", "CPU / 2P", IC_TTT, &appTtt}, {"BUBBLES", "POP POP", IC_BUBBLES, &appBubbles},
                               {"ORACLE", "DICE COIN ASK", IC_ORACLE, &appOracle}, {"FIVE", "WORD IN 6", IC_FIVE, &appFive},
                               {"CODE", "CRACK IT", IC_CODE, &appCode}, {"LIGHTS", "ALL OFF", IC_LIGHTS, &appLights},
                               {"SLIDE", "15 PUZZLE", IC_SLIDE, &appSlide}, {"REFLEX", "REACTION", IC_REFLEX, &appReflex},
                               {"LIFE", "PIXEL LIFE", IC_LIFE, &appLife}}, accGreen);
  for (App* a : {appSudoku, appWords, appPet, appDoodle, appTtt, appBubbles, appOracle, appFive, appCode, appLights, appSlide, appReflex, appLife})
    a->parent = appFun;
  for (App* a : {appCalc, appGraph, appResistor, appElec, appTimer, appMetro, appFlash, appMorse}) a->parent = appTools;
  for (App* a : {appSnake, app2048, appBlocks, appBreakout, appFlappy, appTyping, appRhythm, appStarfall, appMines, appDuel, appEcho, appRunner,
                 appMaze, appFour, appTanks, appRocks, appHopper})
    a->parent = appGames;

  libScanStart();  // music library scan runs in the background during the boot animation
  bootAnim();
  if (!sdOK) toast("NO SD CARD", 2500);
  {  // tell the user (and the SD card) why we rebooted last time
    esp_reset_reason_t rr = esp_reset_reason();
    const char* why = rr == ESP_RST_PANIC ? "CRASH" : rr == ESP_RST_INT_WDT ? "INT WATCHDOG" : rr == ESP_RST_TASK_WDT ? "TASK WATCHDOG" :
                      rr == ESP_RST_WDT ? "WATCHDOG" : rr == ESP_RST_BROWNOUT ? "LOW BATTERY" : nullptr;
    if (why) {
      char b[40]; snprintf(b, sizeof(b), "LAST RESET: %s", why);
      toast(b, 4000);
      if (sdOK) { File f = SD.open("/waveos/crash.txt", FILE_APPEND); if (f) { f.printf("reset=%d %s heap=%u\n", (int)rr, why, (unsigned)ESP.getFreeHeap()); f.close(); } }
    }
  }
  memLog("boot");
  openApp(appHome);
  // draw the first home frame while the backlight is off, then fade in
  cur->draw();
  cv.pushSprite(0, 0);
  M5Cardputer.Display.waitDisplay();
  for (int i = 1; i <= 8; i++) { M5Cardputer.Display.setBrightness(cfg.bright * i / 8); delay(18); }
  lastInput = millis();
}

// ------------------------------------------------------------------ USB dev commands (for screenshots / testing from a PC)
//   SHOT           -> "SHOT 240 135\n" + raw RGB565 big-endian frame
//   K<c>           -> press key c        KENT KESC KTAB KDEL KUP KDN KLT KRT
static void serialDev(std::vector<KeyEv>& keys) {
  static char line[24];
  static int n = 0;
  static char holdC[4]; static uint32_t holdUntil[4]; static int holdN = 0;  // H<c><ms>: hold a key down
  for (int i = 0; i < holdN; i++) if (millis() < holdUntil[i]) heldKeys.push_back(holdC[i]);
  while (Serial.available()) {
    int c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') { if (n < 23) line[n++] = c; continue; }
    line[n] = 0; n = 0;
    if (line[0] == 'H' && line[1]) {
      int slot = holdN < 4 ? holdN++ : 0;
      for (int i = 0; i < holdN; i++) if (millis() >= holdUntil[i]) { slot = i; break; }
      holdC[slot] = line[1]; holdUntil[slot] = millis() + (line[2] ? atoi(line + 2) : 1500);
      continue;
    }
    bool logo = !strcmp(line, "LOGO");  // dev: screenshot of the boot screen
    if (logo) drawBootFrame(1.5f);
    if (logo || !strcmp(line, "SHOT")) {
      Serial.printf("SHOT 240 135\n");
      static uint8_t row[480];
      for (int y = 0; y < 135; y++) {
        for (int x = 0; x < 240; x++) { uint16_t v = cv.readPixel(x, y); row[x * 2] = v >> 8; row[x * 2 + 1] = v & 255; }
        Serial.write(row, 480);
      }
      Serial.flush();
    } else if (line[0] == 'K' && line[1]) {
      KeyEv e;
      const char* k = line + 1;
      if (!strcmp(k, "ENT")) e.code = K_ENTER; else if (!strcmp(k, "ESC")) e.code = K_ESC;
      else if (!strcmp(k, "TAB")) e.code = K_TAB; else if (!strcmp(k, "DEL")) e.code = K_BACK;
      else if (!strcmp(k, "UP")) { e.code = K_CHAR; e.ch = ';'; } else if (!strcmp(k, "DN")) { e.code = K_CHAR; e.ch = '.'; }
      else if (!strcmp(k, "LT")) { e.code = K_CHAR; e.ch = ','; } else if (!strcmp(k, "RT")) { e.code = K_CHAR; e.ch = '/'; }
      else { e.code = K_CHAR; e.ch = k[0]; }
      keys.push_back(e);
    }
  }
}

void loop() {
  uint32_t t0 = millis();
  M5Cardputer.update();
  auto keys = pollKeys();
  serialDev(keys);
  bool g0 = M5Cardputer.BtnA.wasPressed();
  bool anyInput = !keys.empty() || g0 || !heldKeys.empty();
  if (screenOff && !keys.empty()) {  // screen off: media keys work without waking the screen
    bool media = true;
    for (auto& e : keys) {
      char c = e.code == K_CHAR ? e.ch : 0;
      if (c == '-' || c == '_' || c == '.') volumeChange(-1);
      else if (c == '=' || c == '+' || c == ';') volumeChange(1);
      else if (c == ' ' && Music::active()) Music::pause(!Music::paused());
      else media = false;
    }
    if (media) { keys.clear(); anyInput = g0; }
  }
  if (anyInput) {
    lastInput = millis();
    if (saver || screenOff) {  // wake up, swallow the key
      if (screenOff) M5Cardputer.Display.wakeup();
      saver = false; screenOff = false;
      M5Cardputer.Display.setBrightness(cfg.bright);
      keys.clear(); g0 = false;
    }
  }
  for (auto& e : keys) {
    if (e.fn && e.code == K_CHAR && tolower(e.ch) == 'p') { cur->draw(); screenshot(); continue; }
    if (e.fn && e.code == K_CHAR && (e.ch == '-' || e.ch == '_' || e.ch == '=' || e.ch == '+')) { volumeChange(e.ch == '-' || e.ch == '_' ? -1 : 1); continue; }
    if (e.fn && e.code == K_CHAR && (e.ch == '[' || e.ch == ']' || e.ch == '{' || e.ch == '}')) { brightnessChange(e.ch == '[' || e.ch == '{' ? -1 : 1); continue; }
    if (e.fn && e.code == K_CHAR && tolower(e.ch) == 'm') { cfg.mute = !cfg.mute; applyVolume(); toast(cfg.mute ? "MUTED (FN+M)" : "SOUND ON", 900); continue; }
    if (e.fn && e.code == K_CHAR && tolower(e.ch) == 'q') {  // boss key: instant calculator, press again to go back
      static App* before = nullptr;
      if (cur == appCalc && before) { App* b = before; before = nullptr; openApp(b); }
      else { before = cur; openApp(appCalc); }
      continue;
    }
    if (e.fn && e.code == K_CHAR && tolower(e.ch) == 'b') { screenOff = true; M5Cardputer.Display.setBrightness(0); M5Cardputer.Display.sleep(); continue; }
    if (cfg.click && !cur->wantsAllKeys() && !e.rep) Audio::blip(1400, 8, Audio::W_SQUARE, 0.12f);
    bool handled = false;
    try { handled = cur->key(e); }
    catch (std::bad_alloc&) { char b[40]; snprintf(b, sizeof(b), "OUT OF MEMORY (%uK)", (unsigned)(ESP.getMaxAllocHeap() / 1024)); toast(b, 2500); memLog("oom-key"); handled = true; }
    if (!handled && e.code == K_ESC) goBack();
  }
  if (g0) goHome();
  musicTick();
  settingsAutosave();
  timerTick();
  static uint32_t memT = 0;
  if (millis() - memT > 5000) {  // heartbeat over USB serial (for debugging memory)
    memT = millis();
    Serial.printf("[mem] tick free=%u maxblock=%u minfree=%u app=%s music=%d\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
                  (unsigned)ESP.getMinFreeHeap(), cur->name, Music::active() && !Music::paused());
  }
  lowBatteryTick();
  irTick();
  try { cur->tick(); } catch (std::bad_alloc&) { toast("OUT OF MEMORY", 2500); }

  uint32_t idle = millis() - lastInput;
  bool busy = (Music::active() && !Music::paused()) || Looper::playing || Looper::counting || cur == appFlash;
  if (cfg.sleepMin && !busy && idle > cfg.sleepMin * 60000UL) deepSleep();
  if (!Batt::usb && Batt::pct >= 0 && Batt::mv > 2500 && Batt::mv < 3320 && millis() > 30000) deepSleep();  // protect the battery
  if (!saver && !screenOff && cfg.dimSec && idle > cfg.dimSec * 1000UL && cur != appMetro && cur != appFlash) {
    saver = true;
    M5Cardputer.Display.setBrightness(std::max(10, cfg.bright / 4));
  }
  // music playing + idle: switch the screen off (the backlight is the biggest power draw)
  bool musicOn = Music::active() && !Music::paused();
  if (!screenOff && musicOn && cfg.musicOff && idle > cfg.musicOff * 1000UL && cur != appMetro) {
    saver = false; screenOff = true; M5Cardputer.Display.setBrightness(0); M5Cardputer.Display.sleep();
  }
  powerTick(screenOff);
  uint32_t offAfter = busy ? 30000UL : 120000UL;
  if (saver && idle > cfg.dimSec * 1000UL + offAfter) { saver = false; screenOff = true; M5Cardputer.Display.setBrightness(0); M5Cardputer.Display.sleep(); }

  if (screenOff) { delay(70); return; }  // keys are still polled, audio keeps running
  if (saver) drawSaver();
  else {
    try { cur->draw(); } catch (std::bad_alloc&) { cv.fillSprite(T.bg0); textC(120, 60, "OUT OF MEMORY", T.red); }
    drawToast(); drawVolume();
  }
  cv.pushSprite(0, 0);
  uint32_t frame = saver ? 125 : (cur == appMusic || cur == appHome || cur == appNotes || cur == appSettings) ? 50 : 33;
  uint32_t dt = millis() - t0;
  if (dt < frame) delay(frame - dt);
}

String clockString() { return String(); }  // WiFi clock removed (RAM); kept for the status bar API
App* appWifi = nullptr;
