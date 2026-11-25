// WAVE OS - FUN (quiet games for class): SUDOKU, WORDS (letter guessing), PIXEL PET, DOODLE,
// TIC TAC TOE (vs CPU / 2 players), BUBBLES (pop pop), ORACLE (dice, coin, ask, random picker)
#include "common.h"
#include "audio.h"
#include <algorithm>

static KeyCode fdir(const KeyEv& e) {
  KeyCode k = e.nav();
  if (k >= K_UP && k <= K_RIGHT) return k;
  return K_NONE;
}
static void fpanel(int cx, const char* t, const char* sub, uint16_t c) {
  int w = std::max(textW(t, 2), textW(sub)) + 20;
  panel(cx - w / 2, 46, w, 40, c, T.bg1);
  textC(cx, 52, t, c, 2);
  textC(cx, 72, sub, T.grey, 1, false);
}

// ===================================================================== SUDOKU
struct SudokuApp : App {
  uint8_t sol[81], grid[81], given[81];
  int cur = 40, level = 1, best = 0;
  bool won = false, check = false;
  uint32_t t0 = 0, tEnd = 0;
  SudokuApp() { name = "SUDOKU"; }
  void gen() {
    // base pattern + random row/col/band/digit shuffles -> valid solution
    int rows[9], cols[9], d[9];
    int bands[3] = {0, 1, 2}, stacks[3] = {0, 1, 2};
    for (int i = 2; i > 0; i--) { std::swap(bands[i], bands[random(i + 1)]); std::swap(stacks[i], stacks[random(i + 1)]); }
    for (int b = 0; b < 3; b++) {
      int r[3] = {0, 1, 2}, c[3] = {0, 1, 2};
      for (int i = 2; i > 0; i--) { std::swap(r[i], r[random(i + 1)]); std::swap(c[i], c[random(i + 1)]); }
      for (int k = 0; k < 3; k++) { rows[b * 3 + k] = bands[b] * 3 + r[k]; cols[b * 3 + k] = stacks[b] * 3 + c[k]; }
    }
    for (int i = 0; i < 9; i++) d[i] = i + 1;
    for (int i = 8; i > 0; i--) std::swap(d[i], d[random(i + 1)]);
    for (int r = 0; r < 9; r++) for (int c = 0; c < 9; c++) {
      int rr = rows[r], cc = cols[c];
      sol[r * 9 + c] = d[(rr * 3 + rr / 3 + cc) % 9];
    }
    int remove = level == 1 ? 38 : level == 2 ? 46 : 53;
    for (int i = 0; i < 81; i++) { grid[i] = sol[i]; given[i] = 1; }
    int idx[81]; for (int i = 0; i < 81; i++) idx[i] = i;
    for (int i = 80; i > 0; i--) std::swap(idx[i], idx[random(i + 1)]);
    for (int i = 0; i < remove; i++) { grid[idx[i]] = 0; given[idx[i]] = 0; }
    won = false; check = false; t0 = millis();
  }
  bool clash(int i) {  // same number twice in a row / column / box
    int v = grid[i], r = i / 9, c = i % 9;
    if (!v) return false;
    for (int j = 0; j < 81; j++) {
      if (j == i || grid[j] != v) continue;
      int rr = j / 9, cc = j % 9;
      if (rr == r || cc == c || (rr / 3 == r / 3 && cc / 3 == c / 3)) return true;
    }
    return false;
  }
  void enter() override { best = loadScore("sudoku"); static bool g = false; if (!g || won) { g = true; gen(); } }
  bool key(const KeyEv& e) override {
    if (won) { if (e.code == K_ENTER) { gen(); return true; } return e.code != K_ESC; }
    KeyCode k = fdir(e);
    if (k == K_UP) { cur = (cur + 72) % 81; return true; }
    if (k == K_DOWN) { cur = (cur + 9) % 81; return true; }
    if (k == K_LEFT) { cur = cur / 9 * 9 + (cur % 9 + 8) % 9; return true; }
    if (k == K_RIGHT) { cur = cur / 9 * 9 + (cur % 9 + 1) % 9; return true; }
    if (e.code == K_CHAR && e.ch >= '1' && e.ch <= '9' && !given[cur]) {
      grid[cur] = e.ch - '0';
      bool done = true;  // any valid filled grid counts (puzzles can have more than one answer)
      for (int i = 0; i < 81 && done; i++) if (!grid[i] || clash(i)) done = false;
      if (done) { won = true; tEnd = millis(); int s = (tEnd - t0) / 1000; if (!best || s < best) { best = s; saveScore("sudoku", best); } Audio::blip(1320, 200, Audio::W_SQUARE, 0.3f); }
      return true;
    }
    if ((e.code == K_BACK || (e.code == K_CHAR && e.ch == '0')) && !given[cur]) { grid[cur] = 0; return true; }
    if (e.is('c')) { check = !check; return true; }
    if (e.is('n')) { level = level % 3 + 1; gen(); toast(level == 1 ? "EASY" : level == 2 ? "MEDIUM" : "HARD", 700); return true; }
    if (e.is('h') && !given[cur]) { grid[cur] = sol[cur]; return true; }  // hint
    return false;
  }
  void draw() override {
    background(true);
    statusBar("SUDOKU", T.green);
    int cs = 12, ox = 6, oy = 14;
    int selV = grid[cur];
    for (int i = 0; i < 81; i++) {
      int r = i / 9, c = i % 9, x = ox + c * cs + (c / 3), y = oy + r * cs + (r / 3);
      bool sel = i == cur, same = selV && grid[i] == selV;
      bool rel = r == cur / 9 || c == cur % 9 || (r / 3 == cur / 27 && c / 3 == (cur % 9) / 3);
      uint16_t bg = sel ? lerpc(T.panel, T.green, 0.45f) : same ? lerpc(T.panel, T.green, 0.2f) : rel ? T.bg2 : T.panel;
      cv.fillRect(x, y, cs - 1, cs - 1, bg);
      if (grid[i]) {
        char b[2] = {(char)('0' + grid[i]), 0};
        uint16_t col = given[i] ? T.white : (check && clash(i)) ? T.red : T.cyan;
        text(x + 3, y + 2, b, col, 1, false);
      }
    }
    char b[32];
    uint32_t s = ((won ? tEnd : millis()) - t0) / 1000;
    snprintf(b, sizeof(b), "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60)); text(130, 20, b, T.white, 2);
    text(130, 42, level == 1 ? "EASY" : level == 2 ? "MEDIUM" : "HARD", T.green, 1, false);
    int left = 0; for (int i = 0; i < 81; i++) if (!grid[i]) left++;
    snprintf(b, sizeof(b), "%d EMPTY", left); text(130, 54, b, T.grey, 1, false);
    if (best) { snprintf(b, sizeof(b), "BEST %d:%02d", best / 60, best % 60); text(130, 66, b, T.yel, 1, false); }
    text(130, 82, "1-9 FILL  0 CLEAR", T.dim, 1, false);
    text(130, 92, "C CHECK  H HINT", T.dim, 1, false);
    text(130, 102, "N NEW/LEVEL", T.dim, 1, false);
    if (won) fpanel(120, "SOLVED!", "OK = NEW PUZZLE", T.green);
    hintBar({{"ARROWS", "MOVE"}, {"1-9", "NUMBER"}, {"C", "CHECK"}, {"N", "NEW"}});
  }
};

// ===================================================================== WORDS (guess the hidden word letter by letter)
struct WordsApp : App {
  struct Cat { const char* n; const char* w[16]; };
  const Cat cats[6] = {
      {"ANIMALS", {"ELEPHANT", "GIRAFFE", "PENGUIN", "DOLPHIN", "KANGAROO", "OCTOPUS", "CHEETAH", "HAMSTER", "TORTOISE", "FLAMINGO", "SQUIRREL", "PANTHER", "RACCOON", "LOBSTER", "BUTTERFLY", "HEDGEHOG"}},
      {"COUNTRIES", {"TAIWAN", "JAPAN", "BRAZIL", "CANADA", "GERMANY", "AUSTRALIA", "MEXICO", "NORWAY", "EGYPT", "VIETNAM", "PORTUGAL", "ICELAND", "KENYA", "THAILAND", "ARGENTINA", "SWEDEN"}},
      {"MUSIC", {"GUITAR", "DRUMMER", "MELODY", "RHYTHM", "CHORUS", "TEMPO", "SYNTH", "PIANO", "VIOLIN", "BASSLINE", "ORCHESTRA", "HARMONY", "MICROPHONE", "AMPLIFIER", "CONCERT", "PLAYLIST"}},
      {"SCHOOL", {"TEACHER", "HOMEWORK", "LIBRARY", "PENCIL", "ERASER", "SCIENCE", "HISTORY", "GEOGRAPHY", "CALCULATOR", "NOTEBOOK", "CLASSROOM", "BACKPACK", "SCISSORS", "CHEMISTRY", "ALGEBRA", "RECESS"}},
      {"TECH", {"KEYBOARD", "PROCESSOR", "BATTERY", "BLUETOOTH", "SOLDER", "RESISTOR", "FIRMWARE", "PIXEL", "ROUTER", "SENSOR", "MONITOR", "ARDUINO", "TRANSISTOR", "SPEAKER", "CHARGER", "DISPLAY"}},
      {"FOOD", {"PANCAKE", "NOODLES", "DUMPLING", "AVOCADO", "SPAGHETTI", "BURRITO", "PINEAPPLE", "CROISSANT", "BROCCOLI", "CHOCOLATE", "POPCORN", "SANDWICH", "BUBBLETEA", "MANGO", "PRETZEL", "OMELETTE"}}};
  String word;
  int cat = 0, wrong = 0, wins = 0, streak = 0;
  bool used[26];
  int state = 0;  // 0 playing, 1 won, 2 lost
  WordsApp() { name = "WORDS"; }
  void next() { cat = random(6); word = cats[cat].w[random(16)]; wrong = 0; memset(used, 0, sizeof(used)); state = 0; }
  void enter() override { if (!word.length()) next(); streak = loadScore("words"); }
  bool solved() { for (char c : word) if (!used[c - 'A']) return false; return true; }
  bool wantsAllKeys() override { return true; }
  bool key(const KeyEv& e) override {
    if (state) { if (e.code == K_ENTER || (e.code == K_CHAR && e.ch == ' ')) { next(); return true; } return e.code != K_ESC; }
    if (e.code != K_CHAR || e.fn || !isalpha((unsigned char)e.ch)) return false;
    int l = toupper(e.ch) - 'A';
    if (used[l]) return true;
    used[l] = true;
    if (word.indexOf((char)('A' + l)) < 0) { wrong++; Audio::blip(180, 80, Audio::W_SAW, 0.25f); }
    else Audio::blip(880, 40, Audio::W_TRI, 0.25f);
    if (solved()) { state = 1; wins++; streak++; if (streak > loadScore("words")) saveScore("words", streak); }
    else if (wrong >= 7) { state = 2; streak = 0; }
    return true;
  }
  void draw() override {
    background();
    statusBar("WORDS", T.green);
    // growing tree: each wrong guess drops a leaf
    int tx = 40, ty = 108;
    cv.fillRect(tx - 2, ty - 40, 5, 40, RGB(120, 80, 40));
    cv.drawLine(tx, ty - 30, tx - 14, ty - 44, RGB(120, 80, 40)); cv.drawLine(tx, ty - 34, tx + 14, ty - 48, RGB(120, 80, 40));
    static const int lx[7] = {-16, 14, -6, 6, -20, 20, 0}, ly[7] = {-48, -52, -58, -60, -40, -44, -66};
    for (int i = 0; i < 7; i++) {
      bool on = i >= wrong;
      cv.fillCircle(tx + lx[i], ty + ly[i], 5, on ? T.green : lerpc(T.bg0, T.yel, 0.3f));
      if (!on) cv.fillCircle(tx + lx[i] + (i % 2 ? 6 : -6), ty - 2, 2, T.yel);  // fallen leaf
    }
    cv.drawFastHLine(10, ty, 70, T.dim);
    text(92, 18, cats[cat].n, T.purp);
    // word blanks
    int n = word.length(), cw = std::min(14, 140 / std::max(1, n)), x0 = 92;
    for (int i = 0; i < n; i++) {
      char c = word[i];
      bool show = used[c - 'A'] || state == 2;
      cv.drawFastHLine(x0 + i * cw, 52, cw - 3, T.grey);
      if (show) { char b[2] = {c, 0}; text(x0 + i * cw + (cw - 8) / 2, 40, b, state == 2 && !used[c - 'A'] ? T.red : T.white, 1, false); }
    }
    // keyboard of used letters
    for (int i = 0; i < 26; i++) {
      int x = 92 + (i % 13) * 11, y = 66 + (i / 13) * 12;
      char b[2] = {(char)('A' + i), 0};
      bool hit = used[i] && word.indexOf((char)('A' + i)) >= 0;
      text(x, y, b, !used[i] ? T.grey : hit ? T.green : T.dim, 1, false);
    }
    char b[32]; snprintf(b, sizeof(b), "LEAVES %d  STREAK %d", 7 - wrong, streak);
    text(92, 96, b, T.yel, 1, false);
    if (state == 1) fpanel(120, "GOT IT!", "SPACE = NEXT WORD", T.green);
    if (state == 2) fpanel(120, word.c_str(), "SPACE = NEXT WORD", T.red);
    hintBar({{"A-Z", "GUESS"}, {"7", "LEAVES"}, {"ESC", "BACK"}});
  }
};

// ===================================================================== PIXEL PET
struct PetApp : App {
  float food = 80, fun = 80, energy = 80, clean = 80;
  uint32_t ageMin = 0, lastT = 0, lastSave = 0, actT = 0;
  bool sleeping = false;
  int act = -1;  // last action animation
  String nameStr = "BLIP";
  PetApp() { name = "PET"; }
  void load() {
    File f = SD.open("/waveos/pet.txt");
    if (!f) return;
    food = f.parseFloat(); fun = f.parseFloat(); energy = f.parseFloat(); clean = f.parseFloat(); ageMin = f.parseInt();
    f.close();
    // a little time passed while we were away
    food = std::max(20.f, food - 10); fun = std::max(20.f, fun - 10);
  }
  void save() {
    if (!sdOK) return;
    File f = SD.open("/waveos/pet.txt", FILE_WRITE);
    if (f) { f.printf("%.1f %.1f %.1f %.1f %u\n", food, fun, energy, clean, (unsigned)ageMin); f.close(); }
  }
  void enter() override { static bool l = false; if (!l) { l = true; load(); } lastT = millis(); }
  void leave() override { save(); }
  void tick() override {
    float dt = (millis() - lastT) / 60000.f;  // minutes
    lastT = millis();
    static float ageAcc = 0;
    ageAcc += dt; if (ageAcc >= 1) { ageMin += (int)ageAcc; ageAcc -= (int)ageAcc; }
    if (sleeping) { energy = std::min(100.f, energy + 12 * dt); food -= 1 * dt; if (energy >= 100) sleeping = false; }
    else { food -= 3 * dt; fun -= 4 * dt; energy -= 2.5f * dt; clean -= 2 * dt; }
    food = constrain(food, 0.f, 100.f); fun = constrain(fun, 0.f, 100.f); energy = constrain(energy, 0.f, 100.f); clean = constrain(clean, 0.f, 100.f);
    if (millis() - lastSave > 60000) { lastSave = millis(); save(); }
  }
  bool key(const KeyEv& e) override {
    if (e.code != K_CHAR) return false;
    char c = tolower(e.ch);
    if (sleeping && c != 's') { toast("SHHH... SLEEPING (S = WAKE)", 900); return true; }
    switch (c) {
      case 'f': food = std::min(100.f, food + 25); act = 0; Audio::blip(660, 60, Audio::W_TRI, 0.2f); break;
      case 'p': fun = std::min(100.f, fun + 20); energy = std::max(0.f, energy - 5); act = 1; Audio::blip(990, 60, Audio::W_SQUARE, 0.2f); break;
      case 'c': clean = 100; act = 2; Audio::blip(1320, 80, Audio::W_SINE, 0.2f); break;
      case 's': sleeping = !sleeping; act = 3; break;
      default: return false;
    }
    actT = millis();
    return true;
  }
  int mood() { float m = (food + fun + energy + clean) / 4; return m > 70 ? 2 : m > 40 ? 1 : 0; }
  void bar(int x, int y, const char* n, float v, uint16_t c) {
    text(x, y, n, T.grey, 1, false);
    cv.fillRect(x + 34, y + 1, 50, 5, T.dim);
    cv.fillRect(x + 34, y + 1, (int)(50 * v / 100), 5, v < 25 ? T.red : c);
  }
  void draw() override {
    background();
    statusBar("PIXEL PET", T.green);
    int stage = ageMin < 30 ? 0 : ageMin < 180 ? 1 : 2;  // egg-baby, kid, adult
    int cx = 70, cy = 66;
    float bob = sleeping ? 0 : sinf(millis() / 300.f) * 2;
    int sz = stage == 0 ? 14 : stage == 1 ? 18 : 22;
    uint16_t body = stage == 2 ? T.purp : stage == 1 ? T.blue : T.cyan;
    cv.fillEllipse(cx, cy + 22, sz, 4, T.bg2);  // shadow
    cv.fillRoundRect(cx - sz, cy - sz + (int)bob, sz * 2, sz * 2, sz / 2, body);
    cv.fillRoundRect(cx - sz + 3, cy - sz + 3 + (int)bob, sz * 2 - 6, 5, 3, lerpc(body, T.white, 0.35f));
    if (stage >= 1) { cv.fillTriangle(cx - sz + 2, cy - sz + (int)bob, cx - sz + 8, cy - sz + (int)bob, cx - sz + 4, cy - sz - 7 + (int)bob, body);
                      cv.fillTriangle(cx + sz - 2, cy - sz + (int)bob, cx + sz - 8, cy - sz + (int)bob, cx + sz - 4, cy - sz - 7 + (int)bob, body); }
    int ey = cy - 4 + (int)bob;
    bool blink = (millis() / 150) % 25 == 0;
    if (sleeping || blink) { cv.drawFastHLine(cx - 9, ey, 5, T.bg0); cv.drawFastHLine(cx + 4, ey, 5, T.bg0); }
    else { cv.fillRect(cx - 8, ey - 3, 4, 5, T.bg0); cv.fillRect(cx + 4, ey - 3, 4, 5, T.bg0); cv.drawPixel(cx - 7, ey - 2, T.white); cv.drawPixel(cx + 5, ey - 2, T.white); }
    int m = mood(), my = cy + 7 + (int)bob;
    if (m == 2) { cv.drawLine(cx - 5, my, cx, my + 3, T.bg0); cv.drawLine(cx, my + 3, cx + 5, my, T.bg0); }
    else if (m == 1) cv.drawFastHLine(cx - 4, my + 1, 8, T.bg0);
    else { cv.drawLine(cx - 5, my + 3, cx, my, T.bg0); cv.drawLine(cx, my, cx + 5, my + 3, T.bg0); }
    if (clean < 35) for (int i = 0; i < 3; i++) cv.fillCircle(cx - sz - 6 + i * 4, cy + 14, 1, RGB(120, 90, 40));
    if (sleeping) { int z = (millis() / 400) % 3; text(cx + sz, cy - sz - 4 - z * 4, "Z", T.white, 1, false); }
    if (millis() - actT < 900) {
      const char* fx[4] = {"YUM!", "YAY!", "*SPARKLE*", sleeping ? "ZZZ" : "MORNING!"};
      if (act >= 0) textC(cx, 18, fx[act], T.yel);
    }
    int px = 134;
    panel(px - 4, 15, 108, 72, T.edge);
    bar(px, 20, "FOOD", food, T.green);
    bar(px, 32, "FUN", fun, T.yel);
    bar(px, 44, "SLEEP", energy, T.cyan);
    bar(px, 56, "CLEAN", clean, T.blue);
    char b[32];
    snprintf(b, sizeof(b), "AGE %uH%02uM", (unsigned)(ageMin / 60), (unsigned)(ageMin % 60)); text(px, 70, b, T.grey, 1, false);
    text(px, 94, stage == 0 ? "BABY" : stage == 1 ? "KID" : "ADULT", body);
    text(px, 104, m == 2 ? "HAPPY :)" : m == 1 ? "OK" : "NEEDS YOU!", m == 0 ? T.red : T.white, 1, false);
    hintBar({{"F", "FEED"}, {"P", "PLAY"}, {"C", "CLEAN"}, {"S", "SLEEP"}});
  }
};

// ===================================================================== DOODLE (pixel art, saves BMP)
struct DoodleApp : App {
  static constexpr int CW = 48, CH = 22, PXS = 5, OX = 0, OY = 13;
  uint8_t px[CH][CW];
  int cx = 24, cy = 11, col = 1;
  bool pen = false;
  DoodleApp() { name = "DOODLE"; memset(px, 0, sizeof(px)); }
  uint16_t pal(int i) {
    static const uint16_t P[10] = {0, RGB(240, 240, 240), RGB(255, 80, 100), RGB(255, 170, 60), RGB(255, 220, 80), RGB(90, 220, 120),
                                   RGB(62, 232, 240), RGB(74, 140, 255), RGB(154, 92, 255), RGB(255, 120, 200)};
    return i == 0 ? T.bg0 : P[i];
  }
  void fill(int x, int y, int from, int to) {
    if (from == to) return;
    std::vector<std::pair<int, int>> st{{x, y}};
    while (!st.empty()) {
      auto [a, b] = st.back(); st.pop_back();
      if (a < 0 || b < 0 || a >= CW || b >= CH || px[b][a] != from) continue;
      px[b][a] = to;
      st.push_back({a + 1, b}); st.push_back({a - 1, b}); st.push_back({a, b + 1}); st.push_back({a, b - 1});
    }
  }
  void saveBmp() {
    if (!sdOK) { toast("NO SD CARD"); return; }
    ensureDir("/doodles");
    char p[32]; int n = 1;
    do { snprintf(p, sizeof(p), "/doodles/doodle%02d.bmp", n++); } while (SD.exists(p) && n < 100);
    File f = SD.open(p, FILE_WRITE);
    if (!f) return;
    const int S = 4, W = CW * S, H = CH * S, rowB = W * 3;
    uint8_t h[54] = {'B', 'M'};
    auto p32 = [&](int o, uint32_t v) { h[o] = v; h[o + 1] = v >> 8; h[o + 2] = v >> 16; h[o + 3] = v >> 24; };
    p32(2, 54 + rowB * H); p32(10, 54); p32(14, 40); p32(18, W); p32(22, H); h[26] = 1; h[28] = 24; p32(34, rowB * H);
    f.write(h, 54);
    uint8_t row[CW * S * 3];
    for (int y = H - 1; y >= 0; y--) {
      for (int x = 0; x < W; x++) { uint16_t c = pal(px[y / S][x / S]); row[x * 3 + 2] = (c >> 11) * 255 / 31; row[x * 3 + 1] = ((c >> 5) & 63) * 255 / 63; row[x * 3] = (c & 31) * 255 / 31; }
      f.write(row, rowB);
    }
    f.close();
    toast(p + 9, 1500);
  }
  bool key(const KeyEv& e) override {
    KeyCode k = fdir(e);
    if (k != K_NONE) {
      if (k == K_UP) cy = (cy + CH - 1) % CH; else if (k == K_DOWN) cy = (cy + 1) % CH; else if (k == K_LEFT) cx = (cx + CW - 1) % CW; else cx = (cx + 1) % CW;
      if (pen) px[cy][cx] = col;
      return true;
    }
    if (e.code == K_ENTER || (e.code == K_CHAR && e.ch == ' ')) { px[cy][cx] = px[cy][cx] == col ? 0 : col; return true; }
    if (e.code == K_TAB) { pen = !pen; if (pen) px[cy][cx] = col; toast(pen ? "PEN DOWN (DRAWS AS YOU MOVE)" : "PEN UP", 800); return true; }
    if (e.code == K_BACK) { px[cy][cx] = 0; return true; }
    if (e.code != K_CHAR) return false;
    if (e.ch >= '0' && e.ch <= '9') { col = e.ch - '0'; return true; }
    if (e.fn && tolower(e.ch) == 's') { saveBmp(); return true; }
    switch (tolower(e.ch)) {
      case 'f': fill(cx, cy, px[cy][cx], col); return true;
      case 'x': memset(px, 0, sizeof(px)); toast("CLEARED", 600); return true;
      case 'm': for (int y = 0; y < CH; y++) for (int x = 0; x < CW / 2; x++) px[y][CW - 1 - x] = px[y][x]; toast("MIRRORED", 600); return true;
    }
    return false;
  }
  void draw() override {
    cv.fillSprite(T.bg0);
    for (int y = 0; y < CH; y++) for (int x = 0; x < CW; x++) {
      int sx = OX + x * PXS, sy = OY + y * PXS;
      if (px[y][x]) cv.fillRect(sx, sy, PXS, PXS, pal(px[y][x]));
      else if ((x + y) % 2 == 0) cv.drawPixel(sx + 2, sy + 2, T.bg2);
    }
    bool bl = (millis() / 300) % 2;
    frame(OX + cx * PXS - 1, OY + cy * PXS - 1, PXS + 2, PXS + 2, bl ? T.white : pal(col == 0 ? 1 : col));
    statusBar("DOODLE", T.green);
    for (int i = 0; i < 10; i++) {  // palette in the status bar
      int x = 70 + i * 9;
      cv.fillRect(x, 2, 7, 7, i == 0 ? T.bg2 : pal(i));
      if (i == col) frame(x - 1, 1, 9, 9, T.white);
    }
    text(162, 2, pen ? "PEN" : "", T.yel, 1, false);
    hintBar({{"SPC", "DOT"}, {"TAB", "PEN"}, {"F", "FILL"}, {"0-9", "COLOR"}, {"FN+S", "SAVE"}});
  }
};

// ===================================================================== TIC TAC TOE (vs CPU or 2 players)
struct TttApp : App {
  int8_t b[9];
  int cur = 4, turn = 1, mode = 0;  // mode 0 vs CPU, 1 two players
  int state = 0, sx = 0, so = 0, sd = 0;  // 0 playing, 1 X wins, 2 O wins, 3 draw
  TttApp() { name = "TIC TAC TOE"; reset(); }
  void reset() { memset(b, 0, sizeof(b)); turn = 1; state = 0; }
  int winner(const int8_t* g) {
    static const int L[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};
    for (auto& l : L) if (g[l[0]] && g[l[0]] == g[l[1]] && g[l[1]] == g[l[2]]) return g[l[0]];
    for (int i = 0; i < 9; i++) if (!g[i]) return 0;
    return 3;
  }
  int mm(int8_t* g, int who) {
    int w = winner(g);
    if (w == 2) return 10; if (w == 1) return -10; if (w == 3) return 0;
    int best = who == 2 ? -99 : 99;
    for (int i = 0; i < 9; i++) if (!g[i]) { g[i] = who; int v = mm(g, 3 - who); g[i] = 0; best = who == 2 ? std::max(best, v) : std::min(best, v); }
    return best;
  }
  void cpu() {
    int bi = -1, bv = -99;
    for (int i = 0; i < 9; i++) if (!b[i]) { b[i] = 2; int v = mm(b, 1) + (random(4) == 0 ? random(3) : 0); b[i] = 0; if (v > bv) { bv = v; bi = i; } }
    if (bi >= 0) b[bi] = 2;
  }
  void after() {
    int w = winner(b);
    if (w) { state = w; if (w == 1) sx++; else if (w == 2) so++; else sd++; Audio::blip(w == 3 ? 440 : 1046, 200, Audio::W_SQUARE, 0.3f); }
  }
  bool key(const KeyEv& e) override {
    if (state) { if (e.code == K_ENTER || (e.code == K_CHAR && e.ch == ' ')) { reset(); return true; } }
    if (e.is('m')) { mode = 1 - mode; reset(); sx = so = sd = 0; toast(mode ? "2 PLAYERS" : "VS CPU", 700); return true; }
    if (state) return e.code != K_ESC;
    KeyCode k = fdir(e);
    if (k == K_UP) { cur = (cur + 6) % 9; return true; }
    if (k == K_DOWN) { cur = (cur + 3) % 9; return true; }
    if (k == K_LEFT) { cur = cur / 3 * 3 + (cur + 2) % 3; return true; }
    if (k == K_RIGHT) { cur = cur / 3 * 3 + (cur + 1) % 3; return true; }
    int pick = -1;
    if (e.code == K_CHAR && e.ch >= '1' && e.ch <= '9') pick = e.ch - '1';
    if (e.code == K_ENTER || (e.code == K_CHAR && e.ch == ' ')) pick = cur;
    if (pick < 0 || b[pick]) return pick >= 0;
    b[pick] = turn; Audio::blip(turn == 1 ? 660 : 440, 40, Audio::W_TRI, 0.25f);
    after();
    if (!state) { if (mode == 0) { cpu(); after(); } else turn = 3 - turn; }
    return true;
  }
  void draw() override {
    background();
    statusBar("TIC TAC TOE", T.green);
    int cs = 32, ox = 30, oy = 18;
    for (int i = 1; i < 3; i++) { cv.fillRect(ox + i * cs - 1, oy, 2, cs * 3, T.edge); cv.fillRect(ox, oy + i * cs - 1, cs * 3, 2, T.edge); }
    for (int i = 0; i < 9; i++) {
      int x = ox + (i % 3) * cs + cs / 2, y = oy + (i / 3) * cs + cs / 2;
      if (i == cur && !state) cv.drawRoundRect(x - 14, y - 14, 28, 28, 4, T.dim);
      if (b[i] == 1) { for (int d = -1; d <= 1; d++) { cv.drawLine(x - 9 + d, y - 9, x + 9 + d, y + 9, T.cyan); cv.drawLine(x + 9 + d, y - 9, x - 9 + d, y + 9, T.cyan); } }
      else if (b[i] == 2) { cv.drawCircle(x, y, 10, T.pink); cv.drawCircle(x, y, 9, T.pink); }
    }
    char s[32];
    text(150, 22, mode ? "2 PLAYERS" : "VS CPU", T.green);
    snprintf(s, sizeof(s), "X %d", sx); text(150, 40, s, T.cyan, 2);
    snprintf(s, sizeof(s), "O %d", so); text(150, 60, s, T.pink, 2);
    snprintf(s, sizeof(s), "DRAW %d", sd); text(150, 82, s, T.grey, 1, false);
    if (!state && mode) text(150, 96, turn == 1 ? "X TURN" : "O TURN", turn == 1 ? T.cyan : T.pink, 1, false);
    if (state) fpanel(120, state == 3 ? "DRAW" : state == 1 ? (mode ? "X WINS!" : "YOU WIN!") : (mode ? "O WINS!" : "CPU WINS"), "SPACE = AGAIN", state == 1 ? T.cyan : state == 2 ? T.pink : T.grey);
    hintBar({{"ARROWS", "MOVE"}, {"OK/1-9", "PLACE"}, {"M", mode ? "VS CPU" : "2 PLAYER"}});
  }
};

// ===================================================================== BUBBLES (endless bubble wrap + speed mode)
struct BubblesApp : App {
  static constexpr int BW = 12, BH = 6;
  uint8_t popped[BH][BW];
  int cx = 0, cy = 0, total = 0, mode = 0, target = 0, score = 0, best = 0;
  uint32_t t0 = 0;
  BubblesApp() { name = "BUBBLES"; memset(popped, 0, sizeof(popped)); }
  void enter() override { best = loadScore("bubbles"); }
  void refill() { memset(popped, 0, sizeof(popped)); }
  void pop(int x, int y) {
    if (popped[y][x]) return;
    popped[y][x] = 1; total++;
    Audio::blip(random(600, 1400), 25, Audio::W_NOISE, 0.25f);
    bool all = true; for (auto& r : popped) for (auto v : r) if (!v) all = false;
    if (all) { refill(); toast("NEW SHEET!", 600); }
  }
  bool key(const KeyEv& e) override {
    if (e.code == K_TAB) { mode = 1 - mode; refill(); score = 0; t0 = millis(); target = random(BW * BH); return true; }
    if (mode == 1) {  // speed: type the letter shown on the glowing bubble
      if (millis() - t0 > 30000) { if (e.code == K_ENTER) { score = 0; t0 = millis(); refill(); } return e.code != K_ESC; }
      if (e.code == K_CHAR && toupper(e.ch) == 'A' + target % 26) { int x = target % BW, y = target / BW; popped[y][x] = 0; pop(x, y); score++; target = random(BW * BH); }
      else if (e.code == K_CHAR && isalpha((unsigned char)e.ch)) { score = std::max(0, score - 1); Audio::blip(150, 40, Audio::W_SAW, 0.2f); }
      return e.code == K_CHAR;
    }
    KeyCode k = fdir(e);
    if (k == K_UP) { cy = (cy + BH - 1) % BH; return true; }
    if (k == K_DOWN) { cy = (cy + 1) % BH; return true; }
    if (k == K_LEFT) { cx = (cx + BW - 1) % BW; return true; }
    if (k == K_RIGHT) { cx = (cx + 1) % BW; return true; }
    if (e.code == K_ENTER || (e.code == K_CHAR && e.ch == ' ')) { pop(cx, cy); cx = (cx + 1) % BW; if (!cx) cy = (cy + 1) % BH; return true; }
    if (e.code == K_CHAR && isalnum((unsigned char)e.ch)) { int x = random(BW), y = random(BH); pop(x, y); return true; }  // mash any key
    return false;
  }
  void draw() override {
    background();
    statusBar("BUBBLES", T.green);
    int ox = 8, oy = 20, s = 19;
    for (int y = 0; y < BH; y++) for (int x = 0; x < BW; x++) {
      int px = ox + x * s + s / 2, py = oy + y * s + s / 2;
      int idx = y * BW + x;
      if (popped[y][x]) { cv.drawCircle(px, py, 7, T.bg2); cv.drawLine(px - 3, py - 2, px + 2, py + 3, T.dim); continue; }
      bool hl = mode == 1 ? idx == target : (x == cx && y == cy);
      uint16_t c = hl ? T.yel : lerpc(T.bg1, T.cyan, 0.35f);
      cv.fillCircle(px, py, 8, c);
      cv.fillCircle(px - 3, py - 3, 2, lerpc(c, T.white, 0.6f));
      if (mode == 1 && hl) { char b[2] = {(char)('A' + target % 26), 0}; textC(px, py - 3, b, T.bg0, 1, false); }
    }
    char b[40];
    if (mode == 0) { snprintf(b, sizeof(b), "POPPED %d", total); text(80, 2, b, T.white, 1, false); }
    else {
      int left = 30 - (int)((millis() - t0) / 1000);
      if (left < 0) { left = 0; if (score > best) { best = score; saveScore("bubbles", best); } fpanel(120, "TIME!", (String(score) + " POPS - OK AGAIN").c_str(), T.yel); }
      snprintf(b, sizeof(b), "%ds  %d  BEST %d", left, score, best); text(70, 2, b, T.white, 1, false);
    }
    if (mode == 0) hintBar({{"SPC", "POP"}, {"ANY KEY", "RANDOM"}, {"TAB", "SPEED MODE"}});
    else hintBar({{"TYPE", "THE LETTER"}, {"30S", "ROUND"}, {"TAB", "RELAX"}});
  }
};

// ===================================================================== ORACLE: dice, coin, ask a question, random picker
struct OracleApp : App {
  int page = 0;  // 0 dice, 1 coin, 2 ask, 3 pick
  int dice = 2, vals[3] = {1, 1, 1}, coin = 0, pickMax = 30, pickV = 0;
  int ans = -1;
  uint32_t rollT = 0;
  String question;
  const char* answers[20] = {"YES!", "NOPE.", "MAYBE...", "DEFINITELY", "NO WAY", "ASK ME AFTER LUNCH", "PROBABLY", "I WOULDN'T BET ON IT",
                             "THE STARS SAY YES", "100% YES", "NOT TODAY", "ABSOLUTELY", "LOOKS PROMISING", "HMMM... NO", "TRUST YOUR GUT",
                             "NO DOUBT ABOUT IT", "THINK AGAIN", "SURE, WHY NOT", "THE WAVE IS CLOUDY", "ONLY IF YOU STUDY"};
  OracleApp() { name = "ORACLE"; }
  bool wantsAllKeys() override { return page == 2; }
  void roll() { rollT = millis(); Audio::blip(300, 60, Audio::W_NOISE, 0.3f); }
  bool key(const KeyEv& e) override {
    if (e.code == K_TAB) { page = (page + 1) % 4; ans = -1; return true; }
    bool go = e.code == K_ENTER || (e.code == K_CHAR && e.ch == ' ' && page != 2);
    if (page == 2) {
      if (e.code == K_ENTER) { if (question.length()) { ans = random(20); roll(); } return true; }
      if (e.code == K_BACK) { if (question.length()) question.remove(question.length() - 1); ans = -1; return true; }
      if (e.code == K_CHAR && !e.fn && question.length() < 34) { question += e.ch; ans = -1; return true; }
      return false;
    }
    KeyCode k = fdir(e);
    if (page == 0 && (k == K_UP || k == K_DOWN)) { dice = constrain(dice + (k == K_UP ? 1 : -1), 1, 3); return true; }
    if (page == 3 && (k == K_UP || k == K_DOWN || k == K_LEFT || k == K_RIGHT)) {
      pickMax = constrain(pickMax + (k == K_UP ? 1 : k == K_DOWN ? -1 : k == K_RIGHT ? 10 : -10), 2, 999); return true;
    }
    if (go) {
      if (page == 0) for (int i = 0; i < 3; i++) vals[i] = random(1, 7);
      if (page == 1) coin = random(2);
      if (page == 3) pickV = random(1, pickMax + 1);
      roll();
      return true;
    }
    return false;
  }
  void drawDie(int cx, int cy, int v, bool spin) {
    if (spin) v = random(1, 7);
    cv.fillRoundRect(cx - 18, cy - 18, 36, 36, 6, T.white);
    cv.drawRoundRect(cx - 18, cy - 18, 36, 36, 6, T.grey);
    static const int8_t P[7][6][2] = {{}, {{0, 0}}, {{-1, -1}, {1, 1}}, {{-1, -1}, {0, 0}, {1, 1}}, {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}},
                                      {{-1, -1}, {1, -1}, {0, 0}, {-1, 1}, {1, 1}}, {{-1, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {1, 1}}};
    for (int i = 0; i < v; i++) cv.fillCircle(cx + P[v][i][0] * 9, cy + P[v][i][1] * 9, 3, v == 1 ? T.red : T.bg0);
  }
  void draw() override {
    background();
    const char* titles[4] = {"DICE", "COIN FLIP", "ASK THE ORACLE", "RANDOM PICK"};
    statusBar(titles[page], T.green);
    for (int i = 0; i < 4; i++) cv.fillRect(110 + i * 8, 13, 6, 1, i == page ? T.green : T.dim);
    bool spin = millis() - rollT < 500;
    char b[40];
    if (page == 0) {
      int w = dice * 46;
      int sum = 0;
      for (int i = 0; i < dice; i++) { drawDie(120 - w / 2 + 23 + i * 46, 58, vals[i], spin); sum += vals[i]; }
      if (!spin) { snprintf(b, sizeof(b), "TOTAL %d", sum); textC(120, 92, b, T.white, 2); }
      hintBar({{"SPC", "ROLL"}, {"^v", "DICE 1-3"}, {"TAB", "NEXT"}});
    } else if (page == 1) {
      float sq = spin ? fabsf(cosf((millis() - rollT) / 40.f)) : 1;
      int rx = std::max(2, (int)(30 * sq));
      cv.fillEllipse(120, 58, rx, 30, T.yel);
      cv.drawEllipse(120, 58, rx, 30, RGB(200, 150, 40));
      if (!spin) { textC(120, 52, coin ? "T" : "H", RGB(160, 110, 20), 2); textC(120, 96, coin ? "TAILS" : "HEADS", T.white, 2); }
      hintBar({{"SPC", "FLIP"}, {"TAB", "NEXT"}});
    } else if (page == 2) {
      panel(10, 18, 220, 16, T.green, T.panel);
      text(14, 22, (question + ((millis() / 400) % 2 ? "_" : " ")).c_str(), T.white, 1, false);
      cv.fillCircle(120, 72, 28, T.bg0);
      cv.drawCircle(120, 72, 28, T.purp); cv.drawCircle(120, 72, 27, T.purpD);
      cv.fillTriangle(120, 56, 104, 84, 136, 84, lerpc(T.bg0, T.blue, 0.6f));
      if (ans >= 0 && !spin) {
        String a = answers[ans];
        int sp = a.indexOf(' ', a.length() / 2 - 2);
        if (a.length() > 10 && sp > 0) { textC(120, 66, a.substring(0, sp).c_str(), T.white, 1, false); textC(120, 75, a.substring(sp + 1).c_str(), T.white, 1, false); }
        else textC(120, 70, a.c_str(), T.white, 1, false);
      } else textC(120, 70, spin ? "..." : "?", T.white, 1, false);
      hintBar({{"TYPE", "QUESTION"}, {"OK", "ASK"}, {"TAB", "NEXT"}});
    } else {
      snprintf(b, sizeof(b), "1 - %d", pickMax); textC(120, 24, b, T.grey);
      if (pickV || spin) { snprintf(b, sizeof(b), "%d", spin ? (int)random(1, pickMax + 1) : pickV); textC(120, 46, b, spin ? T.grey : T.yel, 5); }
      textC(120, 96, "PICK A STUDENT / QUESTION NUMBER", T.dim, 1, false);
      hintBar({{"SPC", "PICK"}, {"^v<>", "RANGE"}, {"TAB", "NEXT"}});
    }
  }
};

App* appSudoku = new SudokuApp();
App* appWords = new WordsApp();
App* appPet = new PetApp();
App* appDoodle = new DoodleApp();
App* appTtt = new TttApp();
App* appBubbles = new BubblesApp();
App* appOracle = new OracleApp();
