// WAVE OS - DECIDE (in TOOLS): type your options, then spin a wheel or run a this-or-that knockout
#include "common.h"
#include "audio.h"

static const char* const DEC_FILE = "/waveos/decide.txt";
static const char* const PRESETS[][7] = {
    {"PIZZA", "BURGER", "SUSHI", "TACOS", "PASTA", "SALAD", nullptr},
    {"YES", "NO", "MAYBE", nullptr},
    {"WALK", "READ", "GAME", "MUSIC", "NAP", "CODE", nullptr},
    {"MOVIE", "SERIES", "MUSIC", "PODCAST", "BOOK", nullptr},
};
static constexpr int NPRESET = sizeof(PRESETS) / sizeof(PRESETS[0]);

struct DecideApp : App {
  static constexpr int MAXO = 12, MAXL = 16;
  std::vector<String> opt;
  String in;
  int page = 1;          // 0 list, 1 wheel, 2 versus
  int sel = 0, preset = 0;
  bool loaded = false, dirty = false;
  // wheel
  bool out[MAXO] = {};   // knocked out by ELIMINATE
  float rot = 0, vel = 0;
  int lastIdx = -1, winner = -1;
  uint32_t lastT = 0, winT = 0;
  // versus (knockout)
  std::vector<int> round, next;
  int pos = 0, roundNo = 1, champ = -1;

  DecideApp() { name = "DECIDE"; }
  bool wantsAllKeys() override { return page == 0; }

  void enter() override {
    if (!loaded) {
      loaded = true;
      if (sdOK) {
        File f = SD.open(DEC_FILE);
        if (f) {
          while (f.available() && (int)opt.size() < MAXO) {
            String l = f.readStringUntil('\n'); l.trim();
            if (l.length()) opt.push_back(l.substring(0, MAXL));
          }
          f.close();
        }
      }
      if (opt.empty()) loadPreset(0);
      dirty = false;
    }
    if (opt.size() < 2) page = 0;
    resetWheel(); resetVersus();
  }
  void leave() override { save(); vel = 0; }
  void save() {
    if (!dirty || !sdOK) return;
    String* s = new String();
    for (auto& o : opt) { *s += o; *s += '\n'; }
    sdWriteAsync(DEC_FILE, s);
    dirty = false;
  }
  void loadPreset(int p) {
    opt.clear();
    for (int i = 0; PRESETS[p][i]; i++) opt.push_back(PRESETS[p][i]);
    sel = 0; dirty = true;
  }
  void changed() { dirty = true; resetWheel(); resetVersus(); }
  void resetWheel() { memset(out, 0, sizeof(out)); winner = -1; vel = 0; lastIdx = -1; }
  int alive() const { int n = 0; for (int i = 0; i < (int)opt.size(); i++) n += !out[i]; return n; }
  std::vector<int> live() const { std::vector<int> v; for (int i = 0; i < (int)opt.size(); i++) if (!out[i]) v.push_back(i); return v; }
  void resetVersus() {
    round = live(); next.clear(); pos = 0; roundNo = 1; champ = -1;
    for (int i = (int)round.size() - 1; i > 0; i--) std::swap(round[i], round[random(i + 1)]);
    if (round.size() == 1) champ = round[0];
  }

  // ---------------------------------------------------------------- keys
  bool key(const KeyEv& e) override {
    if (e.code == K_TAB) {
      int np = (page + (e.shift ? 2 : 1)) % 3;
      if (np != 0 && opt.size() < 2) { toast("ADD 2+ OPTIONS"); page = 0; return true; }
      page = np; return true;
    }
    if (page == 0) return keyList(e);
    if (page == 1) return keyWheel(e);
    return keyVersus(e);
  }
  bool keyList(const KeyEv& e) {
    KeyCode k = e.navFn();
    int n = opt.size();
    if (k == K_UP && n) { sel = (sel + n - 1) % n; return true; }
    if (k == K_DOWN && n) { sel = (sel + 1) % n; return true; }
    if (e.code == K_ENTER) {
      in.trim();
      if (in.length()) {
        if (n >= MAXO) { toast("MAX 12 OPTIONS"); return true; }
        opt.push_back(in); in = ""; sel = opt.size() - 1; changed();
        Audio::blip(1400, 20, Audio::W_SINE, 0.25f);
      } else if (n >= 2) page = 1;
      else toast("ADD 2+ OPTIONS");
      return true;
    }
    if (e.code == K_BACK) {
      if (in.length()) in.remove(in.length() - 1);
      else if (n) { opt.erase(opt.begin() + sel); sel = std::max(0, std::min(sel, (int)opt.size() - 1)); changed(); }
      return true;
    }
    if (e.code == K_CHAR && e.fn) {
      char c = tolower(e.ch);
      if (c == 'c') { opt.clear(); sel = 0; changed(); toast("CLEARED"); return true; }
      if (c == 'p') { preset = (preset + 1) % NPRESET; loadPreset(preset); changed(); return true; }
      return false;
    }
    if (e.code == K_CHAR && in.length() < MAXL) { in += (char)toupper(e.ch); return true; }
    return false;
  }
  void spin() {
    if (vel > 0) return;
    if (alive() < 2) { toast(alive() ? "ONE LEFT - R RESETS" : "NO OPTIONS"); return; }
    vel = random(720, 1260);
    winner = -1; lastT = millis();
  }
  bool keyWheel(const KeyEv& e) {
    if (e.code == K_ENTER || (e.code == K_CHAR && e.ch == ' ')) { spin(); return true; }
    if (e.is('e') && winner >= 0 && vel == 0) {
      out[winner] = true; winner = -1;
      if (alive() == 1) { winner = live()[0]; winT = millis(); Audio::blip(1760, 200, Audio::W_SINE, 0.4f); }
      return true;
    }
    if (e.is('r')) { resetWheel(); return true; }
    if (e.is('l')) { page = 0; return true; }
    return false;
  }
  void pickVs(int side) {
    if (champ >= 0) return;
    next.push_back(round[pos + side]);
    pos += 2;
    Audio::blip(side ? 990 : 660, 40, Audio::W_SINE, 0.3f);
    if (pos + 1 >= (int)round.size()) {                 // round over (an odd one out gets a bye)
      if (pos < (int)round.size()) next.push_back(round[pos]);
      round = next; next.clear(); pos = 0; roundNo++;
      if (round.size() == 1) { champ = round[0]; winT = millis(); Audio::blip(1320, 220, Audio::W_SINE, 0.4f); }
    }
  }
  bool keyVersus(const KeyEv& e) {
    KeyCode k = e.nav();
    if (k == K_LEFT || e.is('1') || e.is('a')) { pickVs(0); return true; }
    if (k == K_RIGHT || e.is('2') || e.is('d')) { pickVs(1); return true; }
    if (e.code == K_CHAR && e.ch == ' ') { pickVs(random(2)); return true; }  // can't choose: let fate pick
    if (e.is('r') || (e.code == K_ENTER && champ >= 0)) { resetVersus(); return true; }
    if (e.is('l')) { page = 0; return true; }
    return false;
  }

  // ---------------------------------------------------------------- draw
  uint16_t col(int i, int n) {
    const uint16_t P[8] = {T.cyan, T.pink, T.green, T.yel, T.purp, T.blue, T.red, T.white};
    int c = i % 8;
    if (i == n - 1 && n % 8 == 1 && n > 1) c = 3;  // keep the last slice from matching the first
    return P[c];
  }
  void draw() override {
    background();
    const char* titles[3] = {"DECIDE: OPTIONS", "DECIDE: WHEEL", "DECIDE: THIS OR THAT"};
    statusBar(titles[page], T.yel);
    for (int i = 0; i < 3; i++) cv.fillRect(200 + i * 8, 13, 6, 1, i == page ? T.yel : T.dim);
    if (page == 0) drawList();
    else if (page == 1) drawWheel();
    else drawVersus();
  }
  void drawList() {
    panel(3, 16, 234, 16, T.yel, T.bg1);
    if (!in.length()) text(13, 20, "TYPE AN OPTION...", T.dim, 1, false);
    int ex = text(7, 20, in.c_str(), T.white, 1, false);
    if ((millis() / 400) % 2) text(in.length() ? ex : 7, 20, "_", T.yel, 1, false);
    char b[16]; snprintf(b, sizeof(b), "%d/%d", (int)opt.size(), MAXO);
    text(233 - textW(b), 20, b, T.grey, 1, false);
    int n = opt.size();
    for (int i = 0; i < n; i++) {
      int cx = 4 + (i / 6) * 117, cy = 36 + (i % 6) * 14;
      bool s2 = i == sel;
      if (s2) { cv.fillRect(cx, cy - 2, 113, 13, lerpc(T.panel, T.yel, 0.25f)); frame(cx, cy - 2, 113, 13, T.yel); }
      cv.fillRect(cx + 3, cy + 1, 5, 6, col(i, n));
      textFit(cx + 12, cy + 1, opt[i].c_str(), s2 ? T.white : T.grey, 98, false);
    }
    if (!n) textC(120, 70, "NO OPTIONS YET  -  FN+P FOR IDEAS", T.dim, 1, false);
    hintBar({{"ENT", n >= 2 && !in.length() ? "SPIN" : "ADD"}, {"DEL", "REMOVE"}, {"FN+P", "PRESET"}, {"TAB", "MODE"}});
  }
  void drawWheel() {
    const int cx = 62, cy = 70, R = 52;
    std::vector<int> L = live();
    int n = L.size();
    uint32_t now = millis();
    if (vel > 0) {                                          // spin physics: drag + friction
      float dt = std::min(0.05f, (now - lastT) / 1000.f);
      lastT = now;
      rot = fmodf(rot + vel * dt, 360.f);
      vel -= (vel * 0.55f + 40.f) * dt;
      if (vel <= 0) {                                       // never stop on a divider line
        vel = 0;
        float a = fmodf(270.f - rot + 720.f, 360.f), sw0 = 360.f / std::max(1, alive()), off = fmodf(a, sw0);
        float mg = sw0 * 0.2f;
        if (off < mg) rot -= mg - off; else if (off > sw0 - mg) rot += off - (sw0 - mg);
      }
    }
    float sw = n ? 360.f / n : 360.f;
    auto under = [&]() { float a = fmodf(270.f - rot + 720.f, 360.f); return std::min(n - 1, (int)(a / sw)); };
    if (n) {
      int u = under();
      if (vel > 0 && u != lastIdx) { Audio::blip(1500, 6, Audio::W_SQUARE, 0.15f); }
      lastIdx = u;
      if (vel == 0 && winner < 0 && lastT && n > 1) { winner = L[u]; winT = now; lastT = 0; Audio::blip(1320, 160, Audio::W_SINE, 0.4f); }
    }
    cv.fillCircle(cx, cy, R + 2, T.edge);
    for (int j = 0; j < n; j++) {
      float a0 = rot + j * sw, a1 = a0 + sw;
      bool win = winner == L[j] && vel == 0;
      uint16_t c = col(L[j], opt.size());
      if (winner >= 0 && vel == 0 && !win) c = lerpc(T.bg0, c, 0.35f);
      if (n == 1) cv.fillCircle(cx, cy, R, c);
      else cv.fillArc(cx, cy, R, 0, a0, a1, c);
      // label: first letters, along the slice's middle
      float am = (a0 + sw / 2) * DEG_TO_RAD;
      char lb[5]; strncpy(lb, opt[L[j]].c_str(), n > 8 ? 2 : n > 5 ? 3 : 4); lb[n > 8 ? 2 : n > 5 ? 3 : 4] = 0;
      textC(cx + (int)(cosf(am) * R * 0.66f), cy + (int)(sinf(am) * R * 0.66f) - 3, lb, T.bg0, 1, false);
    }
    if (n > 1) for (int j = 0; j < n; j++) {
      float a = (rot + j * sw) * DEG_TO_RAD;
      cv.drawLine(cx, cy, cx + (int)(cosf(a) * R), cy + (int)(sinf(a) * R), T.bg0);
    }
    cv.fillCircle(cx, cy, 7, T.bg1); cv.drawCircle(cx, cy, 7, T.yel);
    cv.fillTriangle(cx - 6, cy - R - 6, cx + 6, cy - R - 6, cx, cy - R + 6, T.white);   // pointer
    cv.drawTriangle(cx - 6, cy - R - 6, cx + 6, cy - R - 6, cx, cy - R + 6, T.bg0);
    // right side: result + list
    int x = 124;
    if (winner >= 0 && vel == 0) {
      bool pop = now - winT < 300;
      uint16_t c = col(winner, opt.size());
      panel(x, 16, 113, 30, c, lerpc(T.panel, c, pop ? 0.5f : 0.2f));
      text(x + 4, 18, alive() == 1 ? "LAST ONE STANDING" : "THE WHEEL SAYS", T.grey, 1, false);
      const char* w = opt[winner].c_str();
      if (textW(w, 2) <= 105) text(x + 4, 28, w, T.white, 2); else textFit(x + 4, 32, w, T.white, 105);
    } else {
      panel(x, 16, 113, 30, T.edge, T.panel);
      textC(x + 56, 26, vel > 0 ? "SPINNING..." : "SPACE TO SPIN", vel > 0 ? T.yel : T.grey, 1, false);
    }
    int m = opt.size();
    for (int i = 0; i < m; i++) {
      bool two = m > 9;                                     // 10-12 options: two columns
      int y = two ? 50 + (i % 6) * 12 : 50 + i * 8, xx = two ? x + (i / 6) * 58 : x;
      uint16_t c = out[i] ? T.dim : (i == winner && vel == 0 ? T.white : T.grey);
      cv.fillRect(xx, y + 1, 4, 5, out[i] ? T.dim : col(i, m));
      textFit(xx + 7, y, opt[i].c_str(), c, m > 9 ? 50 : 105, false);
      if (out[i]) cv.drawFastHLine(xx + 7, y + 3, std::min(m > 9 ? 50 : 105, textW(opt[i].c_str())), T.dim);
    }
    if (winner >= 0 && vel == 0 && alive() > 1) hintBar({{"SPC", "AGAIN"}, {"E", "ELIMINATE"}, {"R", "RESET"}, {"L", "LIST"}});
    else hintBar({{"SPC", "SPIN"}, {"R", "RESET"}, {"L", "EDIT LIST"}, {"TAB", "MODE"}});
  }
  void card(int x, const String& s, uint16_t c, const char* k) {
    panel(x, 40, 100, 50, c, lerpc(T.panel, c, 0.15f));
    const char* t = s.c_str();
    if (textW(t, 2) <= 92) textC(x + 50, 58, t, T.white, 2); else textFit(x + 4, 62, t, T.white, 92);
    cv.fillRect(x + 42, 93, 16, 10, T.edge);
    textC(x + 50, 94, k, T.white, 1, false);
  }
  void drawVersus() {
    char b[40];
    if (champ >= 0) {
      bool pop = millis() - winT < 300;
      uint16_t c = col(champ, opt.size());
      textC(120, 26, "YOUR PICK", T.grey, 1, false);
      panel(30, 40, 180, 44, c, lerpc(T.panel, c, pop ? 0.5f : 0.2f));
      const char* t = opt[champ].c_str();
      if (textW(t, 2) <= 170) textC(120, 54, t, T.white, 2); else textFit(34, 58, t, T.white, 172);
      snprintf(b, sizeof(b), "WON %d ROUNDS", roundNo - 1);
      textC(120, 96, b, T.dim, 1, false);
      hintBar({{"ENT", "AGAIN"}, {"TAB", "MODE"}, {"L", "EDIT LIST"}});
      return;
    }
    int matches = round.size() / 2;
    snprintf(b, sizeof(b), "ROUND %d   MATCH %d/%d", roundNo, pos / 2 + 1, matches);
    textC(120, 20, b, T.grey, 1, false);
    textC(120, 30, "WHICH DO YOU PREFER?", T.white, 1, false);
    int a = round[pos], c2 = round[pos + 1];
    card(8, opt[a], col(a, opt.size()), "<");
    card(132, opt[c2], col(c2, opt.size()), ">");
    textC(120, 58, "VS", T.yel, 2);
    hintBar({{"<", "LEFT"}, {">", "RIGHT"}, {"SPC", "CAN'T PICK"}, {"R", "RESTART"}});
  }
};

App* appDecide = new DecideApp();
