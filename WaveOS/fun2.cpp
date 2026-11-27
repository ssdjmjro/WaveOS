// WAVE OS - FUN pack 2 (small + quiet): LIGHTS (lights out), SLIDE (15 puzzle), REFLEX (reaction test),
// CODE (crack the 4-colour code), FIVE (guess the 5-letter word), LIFE (cellular automaton toy)
// Everything here is tiny: big buffers are allocated on enter() and freed on leave().
#include "common.h"
#include "audio.h"
#include <algorithm>

static void f2panel(int cx, const char* t, const char* sub, uint16_t c) {
  int w = std::max(textW(t, 2), textW(sub)) + 20;
  panel(cx - w / 2, 46, w, 40, c, T.bg1);
  textC(cx, 52, t, c, 2);
  textC(cx, 72, sub, T.grey, 1, false);
}

// ===================================================================== LIGHTS (turn every light off)
struct LightsApp : App {
  uint8_t g[5][5];
  int cx = 2, cy = 2, moves = 0, level = 1, best = 0;
  bool won = false;
  LightsApp() { name = "LIGHTS"; }
  void flip(int x, int y) {
    const int d[5][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto& k : d) { int a = x + k[0], b = y + k[1]; if (a >= 0 && b >= 0 && a < 5 && b < 5) g[b][a] ^= 1; }
  }
  void gen() {  // random presses from "all off" -> always solvable
    memset(g, 0, sizeof(g));
    int n = 3 + level * 2;
    for (int i = 0; i < n; i++) flip(random(5), random(5));
    bool any = false; for (auto& r : g) for (auto v : r) if (v) any = true;
    if (!any) flip(2, 2);
    moves = 0; won = false;
  }
  bool once = false;
  const char* saveKey() override { return "lights"; }
  void persist(Persist& p) override { p.io(g); p.io(cx); p.io(cy); p.io(moves); p.io(level); p.io(won); p.io(once); }
  void enter() override { best = loadScore("lights"); if (!once || won) { once = true; gen(); } }
  bool key(const KeyEv& e) override {
    if (won) { if (e.code == K_ENTER || (e.code == K_CHAR && e.ch == ' ')) { level++; gen(); return true; } return e.code != K_ESC; }
    KeyCode k = e.nav();
    if (k == K_UP) { cy = (cy + 4) % 5; return true; }
    if (k == K_DOWN) { cy = (cy + 1) % 5; return true; }
    if (k == K_LEFT) { cx = (cx + 4) % 5; return true; }
    if (k == K_RIGHT) { cx = (cx + 1) % 5; return true; }
    if (e.code == K_ENTER || (e.code == K_CHAR && e.ch == ' ')) {
      flip(cx, cy); moves++;
      Audio::blip(500 + cx * 80 + cy * 40, 25, Audio::W_TRI, 0.2f);
      bool any = false; for (auto& r : g) for (auto v : r) if (v) any = true;
      if (!any) { won = true; if (level > best) { best = level; saveScore("lights", best); } Audio::blip(1320, 200, Audio::W_SQUARE, 0.3f); }
      return true;
    }
    if (e.is('n')) { gen(); return true; }
    return false;
  }
  void draw() override {
    background();
    statusBar("LIGHTS", T.green);
    int cs = 21, ox = 10, oy = 16;
    for (int y = 0; y < 5; y++) for (int x = 0; x < 5; x++) {
      int px = ox + x * cs, py = oy + y * cs;
      bool on = g[y][x];
      uint16_t c = on ? T.yel : T.bg2;
      cv.fillRoundRect(px, py, cs - 3, cs - 3, 4, c);
      if (on) { cv.drawRoundRect(px - 1, py - 1, cs - 1, cs - 1, 5, lerpc(T.bg0, T.yel, 0.5f)); cv.fillRect(px + 3, py + 3, 4, 2, T.white); }
      if (x == cx && y == cy && !won) cv.drawRoundRect(px - 2, py - 2, cs + 1, cs + 1, 5, T.cyan);
    }
    int lit = 0; for (auto& r : g) for (auto v : r) lit += v;
    char b[32];
    text(130, 18, "TURN ALL", T.white, 1, false);
    text(130, 28, "LIGHTS OFF", T.white, 1, false);
    snprintf(b, sizeof(b), "LEVEL %d", level); text(130, 46, b, T.green);
    snprintf(b, sizeof(b), "MOVES %d", moves); text(130, 60, b, T.grey, 1, false);
    snprintf(b, sizeof(b), "LIT   %d", lit); text(130, 72, b, T.yel, 1, false);
    snprintf(b, sizeof(b), "BEST LV %d", best); text(130, 84, b, T.dim, 1, false);
    text(130, 100, "A PRESS FLIPS", T.dim, 1, false);
    text(130, 110, "THE + SHAPE", T.dim, 1, false);
    if (won) { snprintf(b, sizeof(b), "%d MOVES - OK NEXT", moves); f2panel(120, "DARK!", b, T.green); }
    hintBar({{"ARROWS", "MOVE"}, {"OK", "PRESS"}, {"N", "NEW"}, {"ESC", "BACK"}});
  }
};

// ===================================================================== SLIDE (15 puzzle)
struct SlideApp : App {
  uint8_t t[16];
  int blank = 15, moves = 0, best = 0;
  uint32_t t0 = 0, tEnd = 0;
  bool won = false, started = false;
  SlideApp() { name = "SLIDE"; }
  void shuffle() {
    for (int i = 0; i < 16; i++) t[i] = (i + 1) % 16;
    blank = 15;
    int last = -1;
    for (int n = 0; n < 300; n++) {  // random legal moves -> always solvable
      int opts[4], k = 0, bx = blank % 4, by = blank / 4;
      if (bx > 0) opts[k++] = blank - 1; if (bx < 3) opts[k++] = blank + 1;
      if (by > 0) opts[k++] = blank - 4; if (by < 3) opts[k++] = blank + 4;
      int m = opts[random(k)];
      if (m == last) { n--; continue; }
      last = blank; std::swap(t[m], t[blank]); blank = m;
    }
    moves = 0; won = false; started = false;
  }
  bool once = false;
  const char* saveKey() override { return "slide"; }
  void persist(Persist& p) override {
    p.io(t); p.io(blank); p.io(moves); p.io(won); p.io(started); p.io(once);
    uint32_t now = millis(); p.clock(t0, won && !p.loading ? tEnd : now); if (p.loading && won) tEnd = now;
  }
  void enter() override { best = loadScore("slide"); if (!once || won) { once = true; shuffle(); } }
  bool solved() { for (int i = 0; i < 15; i++) if (t[i] != i + 1) return false; return true; }
  bool key(const KeyEv& e) override {
    if (won) { if (e.code == K_ENTER || (e.code == K_CHAR && e.ch == ' ')) { shuffle(); return true; } return e.code != K_ESC; }
    KeyCode k = e.nav();
    int bx = blank % 4, by = blank / 4, from = -1;
    // the arrow says which way a tile slides into the gap
    if (k == K_UP && by < 3) from = blank + 4;
    else if (k == K_DOWN && by > 0) from = blank - 4;
    else if (k == K_LEFT && bx < 3) from = blank + 1;
    else if (k == K_RIGHT && bx > 0) from = blank - 1;
    else if (e.is('n')) { shuffle(); return true; }
    else return k != K_NONE && k != K_ESC && k != K_CHAR;
    if (!started) { started = true; t0 = millis(); }
    std::swap(t[from], t[blank]); blank = from; moves++;
    Audio::blip(380 + moves % 8 * 30, 15, Audio::W_TRI, 0.15f);
    if (solved()) {
      won = true; tEnd = millis();
      if (!best || moves < best) { best = moves; saveScore("slide", best); }
      Audio::blip(1320, 200, Audio::W_SQUARE, 0.3f);
    }
    return true;
  }
  void draw() override {
    background();
    statusBar("SLIDE", T.green);
    int cs = 26, ox = 10, oy = 15;
    panel(ox - 3, oy - 2, cs * 4 + 4, cs * 4 + 3, T.edge, T.bg0);
    for (int i = 0; i < 16; i++) {
      if (!t[i]) continue;
      int x = ox + (i % 4) * cs, y = oy + (i / 4) * cs;
      bool home = t[i] == i + 1;
      uint16_t c = gradc((t[i] - 1) / 14.f);
      cv.fillRoundRect(x, y, cs - 2, cs - 2, 4, lerpc(T.panel, c, home ? 0.55f : 0.3f));
      cv.drawRoundRect(x, y, cs - 2, cs - 2, 4, c);
      char b[4]; snprintf(b, sizeof(b), "%d", t[i]);
      textC(x + cs / 2 - 1, y + cs / 2 - 5, b, T.white, 1, true);
    }
    char b[32];
    uint32_t s = started ? ((won ? tEnd : millis()) - t0) / 1000 : 0;
    snprintf(b, sizeof(b), "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60)); text(128, 18, b, T.white, 2);
    snprintf(b, sizeof(b), "MOVES %d", moves); text(128, 40, b, T.cyan, 1, false);
    if (best) { snprintf(b, sizeof(b), "BEST %d", best); text(128, 52, b, T.yel, 1, false); }
    text(128, 70, "PUT 1-15 IN", T.grey, 1, false);
    text(128, 80, "ORDER", T.grey, 1, false);
    text(128, 96, "ARROWS SLIDE A", T.dim, 1, false);
    text(128, 106, "TILE INTO THE GAP", T.dim, 1, false);
    if (won) { snprintf(b, sizeof(b), "%d MOVES - OK AGAIN", moves); f2panel(120, "SOLVED!", b, T.green); }
    hintBar({{"ARROWS", "SLIDE"}, {"N", "SHUFFLE"}, {"ESC", "BACK"}});
  }
};

// ===================================================================== REFLEX (reaction time)
struct ReflexApp : App {
  int state = 0;  // 0 idle, 1 waiting, 2 GO, 3 result, 4 too soon
  uint32_t goAt = 0, goT = 0;
  int last = 0, best = 0, round_ = 0, sum = 0, tries[5] = {0};
  ReflexApp() { name = "REFLEX"; }
  void enter() override { best = loadScore("reflex"); state = 0; round_ = 0; sum = 0; }
  void arm() { state = 1; goAt = millis() + random(1200, 4000); }
  bool key(const KeyEv& e) override {
    bool hit = e.code == K_ENTER || (e.code == K_CHAR && !e.fn) || e.code == K_TAB;
    if (e.code == K_ESC || e.rep) return false;
    if (!hit) return false;
    if (state == 0 || state == 3 || state == 4) { if (state == 3 && round_ >= 5) { round_ = 0; sum = 0; } arm(); return true; }
    if (state == 1) { state = 4; Audio::blip(150, 120, Audio::W_SAW, 0.25f); return true; }
    if (state == 2) {
      last = millis() - goT;
      tries[round_ % 5] = last; round_++; sum += last;
      state = 3;
      Audio::blip(1200, 40, Audio::W_SINE, 0.3f);
      if (round_ >= 5) { int avg = sum / 5; if (!best || avg < best) { best = avg; saveScore("reflex", best); } }
      return true;
    }
    return true;
  }
  void tick() override {
    if (state == 1 && millis() >= goAt) { state = 2; goT = millis(); Audio::blip(880, 30, Audio::W_SQUARE, 0.15f); }
  }
  void draw() override {
    uint16_t bg = state == 2 ? lerpc(T.bg0, T.green, 0.6f) : state == 1 ? lerpc(T.bg0, T.red, 0.35f) : state == 4 ? lerpc(T.bg0, T.yel, 0.3f) : T.bg0;
    cv.fillSprite(bg);
    statusBar("REFLEX", T.green);
    char b[40];
    if (state == 0) { textC(120, 40, "REFLEX TEST", T.white, 2); textC(120, 64, "WAIT FOR GREEN, THEN", T.grey, 1, false); textC(120, 75, "PRESS ANY KEY FAST", T.grey, 1, false); textC(120, 94, "5 ROUNDS - ANY KEY TO START", T.dim, 1, false); }
    if (state == 1) textC(120, 52, "WAIT...", T.white, 3);
    if (state == 2) textC(120, 52, "GO!", T.white, 3);
    if (state == 4) { textC(120, 44, "TOO SOON!", T.yel, 2); textC(120, 70, "ANY KEY = TRY AGAIN", T.white, 1, false); }
    if (state == 3) {
      snprintf(b, sizeof(b), "%d MS", last); textC(120, 34, b, T.white, 3);
      const char* r = last < 200 ? "LIGHTNING!" : last < 260 ? "FAST" : last < 350 ? "GOOD" : "SLEEPY?";
      textC(120, 62, r, T.cyan, 1, false);
      if (round_ >= 5) { snprintf(b, sizeof(b), "AVERAGE %d MS", sum / 5); textC(120, 78, b, T.yel, 2); }
      else { snprintf(b, sizeof(b), "ROUND %d/5 - ANY KEY", round_); textC(120, 80, b, T.grey, 1, false); }
    }
    for (int i = 0; i < 5; i++) {
      int x = 70 + i * 22;
      bool done = i < round_ || (round_ >= 5);
      cv.fillRect(x, 104, 18, 3, done ? T.green : T.dim);
    }
    snprintf(b, sizeof(b), "BEST AVG %d MS", best);
    hintBar({{"ANY KEY", "REACT"}, {"", best ? b : "5 ROUNDS"}, {"ESC", "BACK"}});
  }
};

// ===================================================================== CODE (guess the hidden 4-colour code)
struct CodeApp : App {
  static constexpr int ROWS = 9;
  uint8_t secret[4], guess[ROWS][4], blackP[ROWS], whiteP[ROWS];
  int row = 0, col = 0, wins = 0;
  int state = 0;  // 0 playing 1 won 2 lost
  CodeApp() { name = "CODE"; }
  uint16_t pc(int v) {
    static const uint16_t P[7] = {0, RGB(255, 80, 100), RGB(255, 170, 60), RGB(255, 225, 80), RGB(90, 220, 120), RGB(74, 150, 255), RGB(190, 100, 255)};
    return v ? P[v] : T.bg2;
  }
  void reset() { for (auto& s : secret) s = random(1, 7); memset(guess, 0, sizeof(guess)); row = col = 0; state = 0; }
  bool once = false;
  const char* saveKey() override { return "code"; }
  void persist(Persist& p) override { p.io(secret); p.io(guess); p.io(blackP); p.io(whiteP); p.io(row); p.io(col); p.io(state); p.io(once); }
  void enter() override { wins = loadScore("code"); if (!once || state) { once = true; reset(); } }
  void score() {
    int b = 0, w = 0, cs[7] = {0}, cg[7] = {0};
    for (int i = 0; i < 4; i++) { if (guess[row][i] == secret[i]) b++; else { cs[secret[i]]++; cg[guess[row][i]]++; } }
    for (int v = 1; v <= 6; v++) w += std::min(cs[v], cg[v]);
    blackP[row] = b; whiteP[row] = w;
    if (b == 4) { state = 1; wins++; saveScore("code", wins, false); Audio::blip(1320, 200, Audio::W_SQUARE, 0.3f); return; }
    Audio::blip(400 + b * 200, 40, Audio::W_TRI, 0.25f);
    if (++row >= ROWS) { state = 2; Audio::blip(150, 300, Audio::W_SAW, 0.3f); }
    col = 0;
  }
  bool key(const KeyEv& e) override {
    if (state) { if (e.code == K_ENTER || (e.code == K_CHAR && e.ch == ' ')) { reset(); return true; } return e.code != K_ESC; }
    KeyCode k = e.nav();
    if (e.code == K_CHAR && e.ch >= '1' && e.ch <= '6') { guess[row][col] = e.ch - '0'; col = std::min(3, col + 1); return true; }
    if (k == K_LEFT) { col = (col + 3) % 4; return true; }
    if (k == K_RIGHT) { col = (col + 1) % 4; return true; }
    if (k == K_UP) { guess[row][col] = guess[row][col] % 6 + 1; return true; }
    if (k == K_DOWN) { guess[row][col] = (guess[row][col] + 4) % 6 + 1; return true; }
    if (e.code == K_BACK) { guess[row][col] = 0; col = std::max(0, col - 1); return true; }
    if (e.code == K_ENTER) {
      for (int i = 0; i < 4; i++) if (!guess[row][i]) { toast("FILL ALL 4 (KEYS 1-6)", 900); return true; }
      score(); return true;
    }
    return false;
  }
  void draw() override {
    background();
    statusBar("CODE", T.green);
    int ox = 8, oy = 14, rh = 12;
    for (int r = 0; r < ROWS; r++) {
      int y = oy + r * rh;
      bool cur = r == row && !state;
      if (cur) cv.fillRect(ox - 2, y - 1, 92, rh - 1, lerpc(T.panel, T.green, 0.18f));
      for (int i = 0; i < 4; i++) {
        int x = ox + 6 + i * 14;
        uint16_t c = pc(guess[r][i]);
        if (guess[r][i]) { cv.fillCircle(x, y + 5, 4, c); cv.drawPixel(x - 1, y + 3, T.white); }
        else cv.drawCircle(x, y + 5, 4, r <= row ? T.dim : T.bg2);
        if (cur && i == col) cv.drawFastHLine(x - 4, y + 11, 9, (millis() / 300) % 2 ? T.white : T.green);
      }
      if (r < row || (r == row && state == 1))
        for (int p = 0; p < 4; p++) {
          int px = ox + 66 + (p % 2) * 6, py = y + 2 + (p / 2) * 5;
          uint16_t c = p < blackP[r] ? T.white : p < blackP[r] + whiteP[r] ? T.red : T.bg2;
          cv.fillCircle(px, py, 2, c);
        }
    }
    int sx = 110;
    text(sx, 16, "CRACK THE CODE", T.white, 1, false);
    for (int v = 1; v <= 6; v++) { cv.fillCircle(sx + 4 + (v - 1) * 18, 34, 5, pc(v)); char b[2] = {(char)('0' + v), 0}; text(sx + 2 + (v - 1) * 18, 42, b, T.grey, 1, false); }
    cv.fillCircle(sx + 4, 60, 2, T.white); text(sx + 10, 57, "RIGHT COLOUR+SPOT", T.grey, 1, false);
    cv.fillCircle(sx + 4, 71, 2, T.red); text(sx + 10, 68, "RIGHT COLOUR ONLY", T.grey, 1, false);
    char b[32]; snprintf(b, sizeof(b), "TRY %d/%d   WINS %d", std::min(row + 1, ROWS), ROWS, wins); text(sx, 84, b, T.yel, 1, false);
    if (state) {
      text(sx, 98, state == 1 ? "CRACKED IT!" : "THE CODE WAS:", state == 1 ? T.green : T.red, 1, false);
      for (int i = 0; i < 4; i++) cv.fillCircle(sx + 80 + i * 11, 101, 4, pc(secret[i]));
      text(sx, 110, "OK = NEW CODE", T.grey, 1, false);
    }
    hintBar({{"1-6", "COLOUR"}, {"<>", "SLOT"}, {"OK", "GUESS"}, {"DEL", "UNDO"}});
  }
};

// ===================================================================== FIVE (guess the five-letter word in 6 tries)
static const char* const FIVE_WORDS[] = {
    "APPLE", "BEACH", "BRAIN", "BREAD", "BRUSH", "CANDY", "CHAIR", "CHALK", "CHESS", "CLOUD", "CLOCK", "CRANE", "CROWN", "DANCE", "DREAM",
    "DRINK", "EARTH", "FEAST", "FIELD", "FLAME", "FLOOR", "FRUIT", "GHOST", "GLASS", "GRAPE", "GRASS", "HEART", "HORSE", "HOUSE", "JUICE",
    "KNIFE", "LASER", "LEMON", "LIGHT", "MAGIC", "MANGO", "MONEY", "MOUSE", "MUSIC", "NIGHT", "NOISE", "OCEAN", "PAINT", "PAPER", "PARTY",
    "PEACH", "PHONE", "PIANO", "PIZZA", "PLANE", "PLANT", "PLATE", "POWER", "QUEEN", "QUIET", "RADIO", "RIVER", "ROBOT", "SALAD", "SCARF",
    "SHARK", "SHEEP", "SHELF", "SHIRT", "SHOES", "SKATE", "SMILE", "SNAKE", "SPACE", "SPOON", "STAGE", "STORM", "STONE", "SUGAR", "TABLE",
    "TIGER", "TOAST", "TOWER", "TRAIN", "TRUCK", "VOICE", "WATCH", "WATER", "WHALE", "WORLD", "ZEBRA", "BLAZE", "BOARD", "BRAVE", "CABLE",
    "CAMEL", "CHARM", "CHEST", "CLIMB", "COAST", "CORAL", "CRISP", "CRUMB", "DELTA", "EAGLE", "FAIRY", "FLASH", "FROST", "GIANT", "GLOVE",
    "HONEY", "IVORY", "JELLY", "KOALA", "LUNAR", "MAPLE", "MEDAL", "NOVEL", "OLIVE", "OTTER", "PANDA", "PEARL", "PIXEL", "PRISM", "RAVEN",
    "ROYAL", "SALSA", "SOLAR", "SPARK", "SPICE", "STEAM", "SWEET", "SWORD", "TEMPO", "THUMB", "TORCH", "ULTRA", "UNITY", "VIVID", "WAVES",
    "YACHT", "YOUTH", "BASIC", "BLOCK", "CHORD", "DRIFT", "FOCUS", "GUEST", "HAPPY", "LUCKY", "METAL", "OPERA", "QUEST", "SCORE", "TRICK",
    "SYNTH", "BEATS", "LOGIC", "VIDEO", "BRICK", "CLOWN", "FENCE", "HOTEL", "LEARN", "MOVIE", "NURSE", "PILOT", "RULER", "SHINE", "TRAIL"};
static constexpr int FIVE_N = sizeof(FIVE_WORDS) / sizeof(FIVE_WORDS[0]);

struct FiveApp : App {
  char word[6] = "";
  char rows[6][6];
  uint8_t marks[6][5];  // 0 none, 1 not in word, 2 elsewhere, 3 right spot
  uint8_t keyMark[26];
  int row = 0, len = 0, state = 0, streak = 0;  // state 0 play 1 win 2 lose
  FiveApp() { name = "FIVE"; }
  bool wantsAllKeys() override { return true; }
  void next() {
    strcpy(word, FIVE_WORDS[random(FIVE_N)]);
    memset(rows, 0, sizeof(rows)); memset(marks, 0, sizeof(marks)); memset(keyMark, 0, sizeof(keyMark));
    row = len = 0; state = 0;
  }
  const char* saveKey() override { return "five"; }
  void persist(Persist& p) override { p.io(word); p.io(rows); p.io(marks); p.io(keyMark); p.io(row); p.io(len); p.io(state); if (p.loading) word[5] = 0; }
  void enter() override { streak = loadScore("five"); if (!word[0] || state) next(); }
  void judge() {
    char* g = rows[row];
    int left[26] = {0};
    for (int i = 0; i < 5; i++) if (g[i] != word[i]) left[word[i] - 'A']++;
    for (int i = 0; i < 5; i++) {  // greens first, then yellows from what is left (handles double letters)
      if (g[i] == word[i]) marks[row][i] = 3;
      else if (left[g[i] - 'A'] > 0) { marks[row][i] = 2; left[g[i] - 'A']--; }
      else marks[row][i] = 1;
      uint8_t& km = keyMark[g[i] - 'A'];
      km = std::max(km, marks[row][i]);
    }
    if (!strncmp(g, word, 5)) { state = 1; streak++; saveScore("five", streak, false); Audio::blip(1320, 200, Audio::W_SQUARE, 0.3f); return; }
    Audio::blip(500, 30, Audio::W_TRI, 0.2f);
    if (++row >= 6) { state = 2; streak = 0; saveScore("five", 0, false); Audio::blip(150, 300, Audio::W_SAW, 0.3f); }
    len = 0;
  }
  bool key(const KeyEv& e) override {
    if (state) { if (e.code == K_ENTER || (e.code == K_CHAR && e.ch == ' ')) { next(); return true; } return e.code != K_ESC; }
    if (e.code == K_BACK) { if (len) rows[row][--len] = 0; return true; }
    if (e.code == K_ENTER) { if (len == 5) judge(); else toast("NEEDS 5 LETTERS", 700); return true; }
    if (e.code == K_CHAR && !e.fn && isalpha((unsigned char)e.ch) && len < 5) { rows[row][len++] = toupper(e.ch); return true; }
    return e.code != K_ESC;
  }
  uint16_t mc(int m) { return m == 3 ? RGB(60, 190, 100) : m == 2 ? RGB(220, 170, 40) : m == 1 ? T.bg2 : T.panel; }
  void draw() override {
    background();
    statusBar("FIVE", T.green);
    int ts = 16, ox = 10, oy = 15;
    for (int r = 0; r < 6; r++) for (int i = 0; i < 5; i++) {
      int x = ox + i * (ts + 2), y = oy + r * (ts + 1);
      cv.fillRect(x, y, ts, ts - 1, mc(marks[r][i]));
      if (!marks[r][i]) frame(x, y, ts, ts - 1, r == row && !state ? (i == len ? T.white : T.edge) : T.bg2);
      if (rows[r][i]) { char b[2] = {rows[r][i], 0}; text(x + 5, y + 4, b, T.white, 1, false); }
    }
    static const char* KB[3] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
    for (int r = 0; r < 3; r++)
      for (int i = 0; KB[r][i]; i++) {
        char c = KB[r][i];
        int x = 106 + r * 5 + i * 13, y = 20 + r * 14;
        uint8_t m = keyMark[c - 'A'];
        cv.fillRect(x, y, 11, 12, m ? mc(m) : T.edge);
        char b[2] = {c, 0}; text(x + 3, y + 3, b, m == 1 ? T.dim : T.white, 1, false);
      }
    char b[32];
    snprintf(b, sizeof(b), "STREAK %d", streak); text(108, 68, b, T.yel, 1, false);
    cv.fillRect(108, 82, 7, 7, mc(3)); text(118, 82, "RIGHT SPOT", T.grey, 1, false);
    cv.fillRect(108, 93, 7, 7, mc(2)); text(118, 93, "IN THE WORD", T.grey, 1, false);
    if (state == 1) text(108, 108, "GOT IT! OK = NEXT", T.green, 1, false);
    if (state == 2) { snprintf(b, sizeof(b), "IT WAS %s", word); text(108, 106, b, T.red, 1, false); text(108, 115, "OK = NEXT", T.grey, 1, false); }
    hintBar({{"A-Z", "TYPE"}, {"OK", "GUESS"}, {"DEL", "ERASE"}, {"6", "TRIES"}});
  }
};

// ===================================================================== LIFE (cellular automaton toy)
struct LifeApp : App {
  static constexpr int W = 48, H = 22, CS = 5, OY = 13;
  std::vector<uint8_t> a, b;  // allocated while open
  bool run = true;
  int cx = 24, cy = 11, speed = 2, gen_ = 0, pop = 0;
  uint32_t lastT = 0;
  LifeApp() { name = "LIFE"; }
  bool ownsPlusMinus() override { return true; }  // speed
  uint8_t& at(std::vector<uint8_t>& g, int x, int y) { return g[((y + H) % H) * W + (x + W) % W]; }
  void randomize() { for (auto& v : a) v = random(100) < 28 ? 1 : 0; gen_ = 0; }
  void enter() override { a.assign(W * H, 0); b.assign(W * H, 0); randomize(); run = true; }
  void leave() override { a.clear(); a.shrink_to_fit(); b.clear(); b.shrink_to_fit(); }
  void stamp(const char* const* p, int h) {
    for (int j = 0; j < h; j++) for (int i = 0; p[j][i]; i++) if (p[j][i] == '#') at(a, cx + i, cy + j) = 1;
  }
  void step() {
    pop = 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
      int n = 0;
      for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) if ((i || j) && at(a, x + i, y + j)) n++;
      uint8_t v = at(a, x, y);
      uint8_t nv = (n == 3 || (v && n == 2)) ? (v ? std::min(v + 1, 9) : 1) : 0;
      b[y * W + x] = nv;
      if (nv) pop++;
    }
    std::swap(a, b);
    gen_++;
  }
  bool key(const KeyEv& e) override {
    if (a.empty()) return false;
    KeyCode k = e.nav();
    if (k == K_UP) { cy = (cy + H - 1) % H; return true; }
    if (k == K_DOWN) { cy = (cy + 1) % H; return true; }
    if (k == K_LEFT) { cx = (cx + W - 1) % W; return true; }
    if (k == K_RIGHT) { cx = (cx + 1) % W; return true; }
    if (e.code == K_CHAR && e.ch == ' ') { run = !run; return true; }
    if (e.code == K_ENTER) { uint8_t& v = at(a, cx, cy); v = v ? 0 : 1; return true; }
    if (e.is('r')) { randomize(); return true; }
    if (e.is('c')) { std::fill(a.begin(), a.end(), 0); gen_ = 0; run = false; return true; }
    if (e.is('s')) { step(); return true; }
    if (e.is('g')) { static const char* G[3] = {".#.", "..#", "###"}; stamp(G, 3); return true; }
    if (e.is('p')) { static const char* P[3] = {".##", "##.", ".#."}; stamp(P, 3); return true; }  // r-pentomino: grows for ages
    if (e.code == K_CHAR && (e.ch == '-' || e.ch == '_')) { speed = std::max(1, speed - 1); return true; }
    if (e.code == K_CHAR && (e.ch == '=' || e.ch == '+')) { speed = std::min(5, speed + 1); return true; }
    return false;
  }
  void tick() override {
    static const uint16_t ms[6] = {0, 600, 300, 150, 80, 40};
    if (run && !a.empty() && millis() - lastT > ms[speed]) { lastT = millis(); step(); }
  }
  void draw() override {
    cv.fillSprite(T.bg0);
    if (!a.empty())
      for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        uint8_t v = a[y * W + x];
        if (v) cv.fillRect(x * CS, OY + y * CS, CS - 1, CS - 1, gradc((v - 1) / 8.f));
      }
    if (!run || (millis() / 400) % 2) frame(cx * CS - 1, OY + cy * CS - 1, CS + 1, CS + 1, T.white);
    statusBar("LIFE", T.green);
    char b[32]; snprintf(b, sizeof(b), "GEN %d  POP %d  %s", gen_, pop, run ? "" : "PAUSED"); text(60, 2, b, T.white, 1, false);
    hintBar({{"SPC", run ? "PAUSE" : "RUN"}, {"OK", "CELL"}, {"R", "RANDOM"}, {"G P", "STAMP"}, {"-=", "SPEED"}});
  }
};

App* appLights = new LightsApp();
App* appSlide = new SlideApp();
App* appReflex = new ReflexApp();
App* appCode = new CodeApp();
App* appFive = new FiveApp();
App* appLife = new LifeApp();
