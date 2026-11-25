// WAVE OS - more games: Blocks (falling blocks), Breakout (keys or tilt), Flappy, Typing, Rhythm
#include "common.h"
#include "audio.h"

static KeyCode gdir(const KeyEv& e) {
  KeyCode k = e.nav();
  if (k >= K_UP && k <= K_RIGHT) return k;
  if (e.code != K_CHAR) return K_NONE;
  switch (tolower(e.ch)) { case 'w': return K_UP; case 's': return K_DOWN; case 'a': return K_LEFT; case 'd': return K_RIGHT; }
  return K_NONE;
}
static void sidePanel(int x, const char* t1, int v1, const char* t2, int v2, uint16_t acc) {
  char b[16];
  panel(x, 15, 236 - x, 36, T.edge);
  text(x + 5, 19, t1, T.grey);
  snprintf(b, sizeof(b), "%d", v1);
  text(x + 5, 31, b, T.white, 2);
  panel(x, 54, 236 - x, 22, T.edge);
  text(x + 5, 58, t2, T.grey);
  snprintf(b, sizeof(b), "%d", v2);
  text(x + 5, 67, b, acc);
}
static void overPanel(int cx, const char* t, const char* sub, uint16_t c) {
  int w = std::max(textW(t, 2), textW(sub)) + 20;
  panel(cx - w / 2, 46, w, 40, c, T.bg1);
  textC(cx, 52, t, c, 2);
  textC(cx, 72, sub, T.grey, 1, false);
}

// ===================================================================== BLOCKS
struct BlocksApp : App {
  static constexpr int BW = 10, BH = 20, CS = 5, GX = 60, GY = 14;
  uint8_t g[BH][BW];
  int px, py, rot, piece, nextP, score = 0, lines = 0, level = 1, best = 0;
  bool over = false, paused = false;
  uint32_t lastFall = 0;
  static const uint16_t SH[7][4];  // 4x4 bitmasks per rotation
  BlocksApp() { name = "BLOCKS"; }
  bool cell(int p, int r, int x, int y) { return SH[p][r] & (0x8000 >> (y * 4 + x)); }
  bool fits(int p, int r, int ox, int oy) {
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++)
      if (cell(p, r, x, y)) {
        int gx = ox + x, gy = oy + y;
        if (gx < 0 || gx >= BW || gy >= BH) return false;
        if (gy >= 0 && g[gy][gx]) return false;
      }
    return true;
  }
  void spawn() {
    piece = nextP; nextP = random(7); rot = 0; px = 3; py = -1;
    if (!fits(piece, rot, px, py)) { over = true; if (score > best) { best = score; saveScore("blocks", best); } Audio::blip(110, 400, Audio::W_SAW); }
  }
  void reset() { memset(g, 0, sizeof(g)); score = lines = 0; level = 1; over = false; paused = false; nextP = random(7); spawn(); }
  void enter() override { best = loadScore("blocks"); if (!score || over) reset(); }
  void lock() {
    bool spill = false;
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++)
      if (cell(piece, rot, x, y)) { if (py + y >= 0) g[py + y][px + x] = piece + 1; else spill = true; }
    int cleared = 0;
    for (int y = BH - 1; y >= 0; y--) {
      bool full = true;
      for (int x = 0; x < BW; x++) if (!g[y][x]) full = false;
      if (full) { cleared++; for (int yy = y; yy > 0; yy--) memcpy(g[yy], g[yy - 1], BW); memset(g[0], 0, BW); y++; }
    }
    static const int pts[5] = {0, 100, 300, 500, 800};
    score += pts[cleared] * level;
    lines += cleared;
    level = 1 + lines / 10;
    if (cleared) Audio::blip(660 + cleared * 220, 90, Audio::W_SQUARE); else Audio::blip(220, 20, Audio::W_TRI, 0.3f);
    if (spill && !cleared) { over = true; if (score > best) { best = score; saveScore("blocks", best); } Audio::blip(110, 400, Audio::W_SAW); return; }
    spawn();
  }
  bool key(const KeyEv& e) override {
    if (over) { if (e.code == K_ENTER) { reset(); return true; } return false; }
    if (e.is('p')) { paused = !paused; return true; }
    if (paused) return false;
    KeyCode k = gdir(e);
    if (k == K_LEFT && fits(piece, rot, px - 1, py)) { px--; return true; }
    if (k == K_RIGHT && fits(piece, rot, px + 1, py)) { px++; return true; }
    if (k == K_DOWN) { if (fits(piece, rot, px, py + 1)) { py++; score++; } else lock(); return true; }
    if (k == K_UP || e.is('x')) {
      int r = (rot + 1) % 4;
      for (int kick : {0, -1, 1, -2, 2}) if (fits(piece, r, px + kick, py)) { rot = r; px += kick; break; }
      return true;
    }
    if (e.is('z')) { int r = (rot + 3) % 4; if (fits(piece, r, px, py)) rot = r; return true; }
    if (e.code == K_CHAR && e.ch == ' ') { while (fits(piece, rot, px, py + 1)) { py++; score += 2; } lock(); return true; }
    return false;
  }
  void tick() override {
    if (over || paused) return;
    uint32_t sp = std::max(80, 600 - (level - 1) * 55);
    if (millis() - lastFall > sp) {
      lastFall = millis();
      if (fits(piece, rot, px, py + 1)) py++; else lock();
    }
  }
  uint16_t pc(int p) { return gradc(p / 6.f); }
  void block(int x, int y, uint16_t c, int s = CS) {
    cv.fillRect(x, y, s - 1, s - 1, c);
    cv.drawFastHLine(x, y, s - 1, lerpc(c, T.white, 0.45f));
  }
  void draw() override {
    background();
    statusBar("BLOCKS", T.blue);
    panel(GX - 2, GY - 1, BW * CS + 4, BH * CS + 3, T.blueD, T.bg0);
    for (int y = 0; y < BH; y++) for (int x = 0; x < BW; x++) {
      if (g[y][x]) block(GX + x * CS, GY + 1 + y * CS, pc(g[y][x] - 1));
      else if ((x + y) & 1) cv.drawPixel(GX + x * CS + 2, GY + 3 + y * CS, T.bg2);
    }
    if (!over) {
      int gy = py;
      while (fits(piece, rot, px, gy + 1)) gy++;
      for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++)
        if (cell(piece, rot, x, y)) {
          if (gy + y >= 0) frame(GX + (px + x) * CS, GY + 1 + (gy + y) * CS, CS - 1, CS - 1, T.dim);
          if (py + y >= 0) block(GX + (px + x) * CS, GY + 1 + (py + y) * CS, pc(piece));
        }
    }
    sidePanel(118, "SCORE", score, "BEST", best, T.yel);
    char b[24];
    panel(118, 79, 118, 44, T.edge);
    text(123, 83, "NEXT", T.grey, 1, false);
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) if (cell(nextP, 0, x, y)) block(126 + x * 6, 94 + y * 6, pc(nextP), 6);
    snprintf(b, sizeof(b), "LV %d", level); text(170, 83, b, T.cyan, 1, false);
    snprintf(b, sizeof(b), "LN %d", lines); text(170, 95, b, T.cyan, 1, false);
    panel(4, 15, 52, 108, T.edge);
    text(8, 19, "^ ROT", T.dim, 1, false); text(8, 30, "<> MOVE", T.dim, 1, false); text(8, 41, "v SOFT", T.dim, 1, false);
    text(8, 52, "SPC", T.dim, 1, false); text(8, 61, " DROP", T.dim, 1, false); text(8, 72, "P PAUSE", T.dim, 1, false);
    if (paused) overPanel(GX + BW * CS / 2, "PAUSED", "P TO GO", T.white);
    if (over) overPanel(GX + BW * CS / 2, "GAME OVER", "OK = AGAIN", T.red);
    hintBar({{"<>", "MOVE"}, {"^", "ROTATE"}, {"SPC", "DROP"}, {"ESC", "BACK"}});
  }
};
// I O T S Z J L, rotations as 4x4 masks
const uint16_t BlocksApp::SH[7][4] = {
    {0x0F00, 0x2222, 0x00F0, 0x4444}, {0x6600, 0x6600, 0x6600, 0x6600}, {0x4E00, 0x4640, 0x0E40, 0x4C40},
    {0x6C00, 0x4620, 0x06C0, 0x8C40}, {0xC600, 0x2640, 0x0C60, 0x4C80}, {0x8E00, 0x6440, 0x0E20, 0x44C0},
    {0x2E00, 0x4460, 0x0E80, 0xC440}};

// ===================================================================== BREAKOUT
struct BreakoutApp : App {
  float bx, by, vx, vy, padX = 120;
  int padW = 30, lives = 3, score = 0, best = 0, level = 1;
  uint8_t bricks[6][12];
  bool stuck = true, over = false, tilt = false;
  BreakoutApp() { name = "BRICKS"; }
  void build() {
    for (int r = 0; r < 6; r++) for (int c = 0; c < 12; c++) bricks[r][c] = (level > 1 && r < level - 1) ? 2 : 1;
  }
  void reset() { lives = 3; score = 0; level = 1; over = false; build(); serve(); }
  void serve() { stuck = true; vx = 1.3f; vy = -1.9f - level * 0.15f; }
  void enter() override { best = loadScore("breakout"); if (over || !score) reset(); }
  bool key(const KeyEv& e) override {
    if (over) { if (e.code == K_ENTER) { reset(); return true; } return false; }
    KeyCode k = gdir(e);
    if (k == K_LEFT) { padX -= e.rep ? 10 : 7; return true; }
    if (k == K_RIGHT) { padX += e.rep ? 10 : 7; return true; }
    if (e.code == K_CHAR && e.ch == ' ') { stuck = false; return true; }
    if (e.is('t')) { float a, b, c; tilt = !tilt && imuAccel(a, b, c); toast(tilt ? "TILT CONTROL ON" : "TILT OFF"); return true; }
    return false;
  }
  void tick() override {
    if (over) return;
    float ax, ay, az;
    if (tilt && imuAccel(ax, ay, az) && fabsf(ax) > 0.06f) padX += ax * (cfg.rot == 3 ? -9.f : 9.f);  // tilt left/right (dead zone)
    for (auto h : heldKeys) { if (h == ',') padX -= 4; if (h == '/') padX += 4; }
    padX = constrain(padX, 4 + padW / 2.f, 236 - padW / 2.f);
    if (stuck) { bx = padX; by = 112; return; }
    for (int step = 0; step < 2; step++) {
      bx += vx; by += vy;
      if (bx < 5) { bx = 5; vx = -vx; }
      if (bx > 235) { bx = 235; vx = -vx; }
      if (by < 14) { by = 14; vy = -vy; }
      if (by >= 114 && by <= 118 && vy > 0 && fabsf(bx - padX) <= padW / 2 + 2) {
        vy = -fabsf(vy);
        vx = (bx - padX) / (padW / 2) * 2.2f;
        if (fabsf(vx) < 0.35f) vx = vx < 0 ? -0.35f : 0.35f;
        Audio::blip(440, 25, Audio::W_SQUARE, 0.3f);
      }
      if (by > 124) {
        lives--; Audio::blip(150, 250, Audio::W_SAW);
        if (lives <= 0) { over = true; if (score > best) { best = score; saveScore("breakout", best); } }
        serve(); return;
      }
      int c = ((int)bx - 6) / 19, r = ((int)by - 22) / 7;
      if (r >= 0 && r < 6 && c >= 0 && c < 12 && bricks[r][c]) {
        bricks[r][c]--;
        vy = -vy;
        score += 10 * (6 - r);
        Audio::blip(500 + (6 - r) * 120, 30, Audio::W_SQUARE, 0.35f);
        bool left = false;
        for (auto& row : bricks) for (auto b : row) if (b) left = true;
        if (!left) { level++; build(); serve(); toast("LEVEL UP!"); }
      }
    }
  }
  void draw() override {
    background();
    statusBar("BRICKS", T.blue);
    char b[32];
    snprintf(b, sizeof(b), "%d", score); text(80, 2, b, T.white, 1, false);
    for (int i = 0; i < lives; i++) cv.fillCircle(130 + i * 7, 5, 2, T.pink);
    for (int r = 0; r < 6; r++) for (int c = 0; c < 12; c++) {
      if (!bricks[r][c]) continue;
      int x = 6 + c * 19, y = 22 + r * 7;
      uint16_t col = bricks[r][c] > 1 ? T.white : gradc(r / 5.f);
      cv.fillRect(x, y, 18, 6, col);
      cv.drawFastHLine(x, y, 18, lerpc(col, T.white, 0.5f));
      cv.drawFastHLine(x, y + 5, 18, lerpc(col, T.bg0, 0.4f));
    }
    int px = (int)padX - padW / 2;
    cv.fillRect(px, 116, padW, 4, T.cyan);
    cv.drawFastHLine(px, 116, padW, T.white);
    cv.fillCircle((int)bx, (int)by, 2, T.white);
    if (stuck && !over) textC(120, 80, "SPACE TO LAUNCH", T.grey);
    if (over) overPanel(120, "GAME OVER", "OK = AGAIN", T.red);
    snprintf(b, sizeof(b), "BEST %d", best);
    hintBar({{"<>", "MOVE"}, {"SPC", "LAUNCH"}, {"T", tilt ? "TILT ON" : "TILT"}, {"", b}});
  }
};

// ===================================================================== FLAPPY
struct FlappyApp : App {
  float y = 60, v = 0;
  struct Pipe { float x; int gap; bool scored; };
  Pipe pipes[3];
  int score = 0, best = 0;
  bool started = false, over = false;
  float scroll = 0;
  FlappyApp() { name = "GLIDER"; }
  void reset() {
    y = 60; v = 0; score = 0; started = false; over = false;
    for (int i = 0; i < 3; i++) pipes[i] = {250.f + i * 90, (int)random(30, 85), false};
  }
  void enter() override { best = loadScore("flappy"); reset(); }
  bool key(const KeyEv& e) override {
    bool flap = (e.code == K_CHAR && e.ch == ' ') || e.nav() == K_UP || e.code == K_ENTER;
    if (!flap) return false;
    if (over) { reset(); return true; }
    started = true; v = -2.6f;
    Audio::blip(780, 25, Audio::W_TRI, 0.3f);
    return true;
  }
  void tick() override {
    if (!started || over) return;
    v += 0.22f; y += v;
    scroll += 1.6f;
    for (auto& p : pipes) {
      p.x -= 1.6f;
      if (p.x < -20) { float mx = 0; for (auto& q : pipes) mx = std::max(mx, q.x); p = {mx + 90, (int)random(30, 85), false}; }
      if (!p.scored && p.x + 16 < 40) { p.scored = true; score++; Audio::blip(1200, 40, Audio::W_SQUARE, 0.3f); }
      if (p.x < 46 && p.x + 18 > 34 && (y - 4 < p.gap - 18 || y + 4 > p.gap + 18)) over = true;
    }
    if (y > 120 || y < 12) over = true;
    if (over) { Audio::blip(160, 300, Audio::W_SAW); if (score > best) { best = score; saveScore("flappy", best); } }
  }
  void draw() override {
    background();
    // parallax wave hills
    for (int x = 0; x < 240; x++) {
      int h = 108 + (int)(6 * sinf((x + scroll * 0.4f) * 0.05f));
      cv.drawFastVLine(x, h, 125 - h, lerpc(T.bg1, T.blueD, 0.6f));
    }
    for (auto& p : pipes) {
      int x = (int)p.x;
      uint16_t c = T.green, d = lerpc(T.green, T.bg0, 0.5f);
      cv.fillRect(x, 12, 18, p.gap - 18 - 12, d); cv.fillRect(x + 2, 12, 4, p.gap - 18 - 12, c);
      cv.fillRect(x - 2, p.gap - 24, 22, 6, c);
      cv.fillRect(x, p.gap + 18, 18, 125 - p.gap - 18, d); cv.fillRect(x + 2, p.gap + 18, 4, 125 - p.gap - 18, c);
      cv.fillRect(x - 2, p.gap + 18, 22, 6, c);
    }
    // bird
    int bxp = 40, byp = (int)y;
    static const char* bird[7] = {"..####..", ".#..#.#.", "#...####", "######..", "#.#####.", ".#....#.", "..####.."};
    for (int j = 0; j < 7; j++) for (int i = 0; i < 8; i++) if (bird[j][i] == '#') cv.drawPixel(bxp - 4 + i, byp - 3 + j, j < 3 ? T.yel : T.pink);
    statusBar("GLIDER", T.blue);
    char b[16];
    snprintf(b, sizeof(b), "%d", score);
    textC(120, 16, b, T.white, 2);
    if (!started) textC(120, 60, "SPACE TO FLAP", T.white);
    if (over) { snprintf(b, sizeof(b), "BEST %d", best); overPanel(120, "OUCH!", b, T.red); }
    hintBar({{"SPC", "FLAP"}, {"ESC", "BACK"}});
  }
};

// ===================================================================== TYPING
struct TypingApp : App {
  static constexpr int NW = 64;
  const char* words[NW] = {"wave", "synth", "cardputer", "pixel", "music", "guitar", "riff", "chord", "loop", "neon", "bass", "drum", "tempo",
                           "solder", "resistor", "voltage", "ohm", "laser", "remote", "signal", "noise", "filter", "delay", "volume", "track",
                           "jungle", "rock", "metal", "tune", "beat", "key", "code", "flash", "esp", "chip", "byte", "screen", "battery",
                           "orbit", "galaxy", "rocket", "sprite", "level", "score", "combo", "power", "speed", "glow", "purple", "cyan",
                           "keyboard", "arduino", "python", "linux", "obsidian", "notes", "graph", "matrix", "vector", "binary", "logic",
                           "amp", "pedal", "string"};
  struct W { String s; float x, y, v; };
  std::vector<W> live;
  String typed;
  int score = 0, best = 0, lives = 5, chars = 0;
  uint32_t startT = 0, lastSpawn = 0;
  bool over = false;
  TypingApp() { name = "TYPING"; }
  bool wantsAllKeys() override { return true; }
  void reset() { live.clear(); typed = ""; score = 0; lives = 5; chars = 0; over = false; startT = millis(); }
  void enter() override { best = loadScore("typing"); reset(); }
  bool key(const KeyEv& e) override {
    if (over) { if (e.code == K_ENTER) { reset(); return true; } return e.code != K_ESC; }
    if (e.code == K_BACK) { if (typed.length()) typed.remove(typed.length() - 1); return true; }
    if (e.code == K_ENTER || (e.code == K_CHAR && e.ch == ' ')) { typed = ""; return true; }
    if (e.code != K_CHAR || e.fn) return false;
    typed += (char)tolower(e.ch);
    for (size_t i = 0; i < live.size(); i++)
      if (live[i].s == typed) {
        score += typed.length() * 10; chars += typed.length() + 1;
        live.erase(live.begin() + i); typed = "";
        Audio::blip(1000 + score % 500, 40, Audio::W_SQUARE, 0.3f);
        return true;
      }
    bool prefix = false;
    for (auto& w : live) if (w.s.startsWith(typed)) prefix = true;
    if (!prefix) { typed = ""; Audio::blip(200, 30, Audio::W_SAW, 0.2f); }
    return true;
  }
  void tick() override {
    if (over) return;
    float lvl = 1 + (millis() - startT) / 30000.f;
    if (millis() - lastSpawn > (uint32_t)(2200 / lvl) && live.size() < 7) {
      lastSpawn = millis();
      String w = words[random(NW)];
      live.push_back({w, (float)random(4, 236 - w.length() * 6), 14, 0.12f * lvl + random(0, 10) / 100.f});
    }
    for (size_t i = 0; i < live.size(); i++) {
      live[i].y += live[i].v;
      if (live[i].y > 112) {
        live.erase(live.begin() + i); i--; lives--;
        Audio::blip(120, 150, Audio::W_SAW);
        if (lives <= 0) { over = true; if (score > best) { best = score; saveScore("typing", best); } }
      }
    }
  }
  void draw() override {
    background();
    statusBar("TYPING", T.blue);
    for (auto& w : live) {
      bool match = typed.length() && w.s.startsWith(typed);
      int x = (int)w.x, y = (int)w.y;
      if (match) {
        text(x, y, typed.c_str(), T.green);
        text(x + typed.length() * 6, y, w.s.substring(typed.length()).c_str(), T.white);
      } else text(x, y, w.s.c_str(), lerpc(T.grey, T.red, (w.y - 14) / 100.f));
    }
    cv.drawFastHLine(0, 113, 240, T.red);
    panel(60, 114, 120, 11, T.cyan, T.bg1);
    textC(120, 116, (typed + ((millis() / 400) % 2 ? "_" : " ")).c_str(), T.cyan, 1, false);
    char b[32];
    float mins = (millis() - startT) / 60000.f;
    snprintf(b, sizeof(b), "%d  WPM %d", score, mins > 0.05f ? (int)(chars / 5 / mins) : 0);
    text(70, 2, b, T.white, 1, false);
    for (int i = 0; i < lives; i++) cv.fillRect(150 + i * 5, 3, 3, 5, T.pink);
    if (over) { snprintf(b, sizeof(b), "BEST %d - OK AGAIN", best); overPanel(120, "GAME OVER", b, T.red); }
    cv.fillRect(0, 125, 240, 10, T.bar);
    text(4, 127, "TYPE THE FALLING WORDS BEFORE THEY LAND", T.dim, 1, false);
  }
};

// ===================================================================== RHYTHM
struct RhythmApp : App {
  static constexpr int LANES = 4;
  const char keys[LANES] = {'d', 'f', 'j', 'k'};
  struct N { float beat; int lane; bool hit, missed; };
  std::vector<N> notes;
  int bpm = 112, score = 0, combo = 0, maxCombo = 0, best = 0, perfect = 0, good = 0, miss = 0;
  uint32_t t0 = 0;
  bool running = false, done = false;
  int lastBeat = -1;
  String judge;
  uint32_t judgeT = 0, laneFlash[LANES] = {0};
  static constexpr int SONG_BEATS = 96;
  const uint8_t scale[8] = {57, 60, 62, 64, 67, 69, 72, 74};  // A minor pentatonic-ish
  RhythmApp() { name = "RHYTHM"; }
  bool wantsAllKeys() override { return true; }
  void gen() {
    notes.clear();
    randomSeed(millis());
    for (int b = 4; b < SONG_BEATS; b++) {
      int dens = b < 32 ? 1 : (b < 64 ? 2 : 3);
      if (random(4) < 3) notes.push_back({(float)b, (int)random(LANES), false, false});
      if (dens >= 2 && random(3) == 0) notes.push_back({b + 0.5f, (int)random(LANES), false, false});
      if (dens >= 3 && random(4) == 0) notes.push_back({b + 0.25f, (int)random(LANES), false, false});
    }
  }
  void start() { gen(); score = combo = maxCombo = perfect = good = miss = 0; t0 = millis() + 1500; running = true; done = false; lastBeat = -1; }
  void enter() override { best = loadScore("rhythm"); running = false; done = false; }
  void leave() override { running = false; Audio::allOff(7); }
  float beatNow() { return ((int32_t)(millis() - t0)) / (60000.f / bpm); }
  bool key(const KeyEv& e) override {
    if (!running) { if (e.code == K_ENTER || (e.code == K_CHAR && e.ch == ' ')) { start(); return true; } return false; }
    if (e.code != K_CHAR) return false;
    int lane = -1;
    for (int i = 0; i < LANES; i++) if (tolower(e.ch) == keys[i]) lane = i;
    if (lane < 0) return false;
    laneFlash[lane] = millis();
    float bn = beatNow();
    float msPerBeat = 60000.f / bpm;
    N* bestN = nullptr;
    float bd = 1e9;
    for (auto& n : notes) if (!n.hit && !n.missed && n.lane == lane) { float d = fabsf(n.beat - bn) * msPerBeat; if (d < bd) { bd = d; bestN = &n; } }
    if (bestN && bd < 140) {
      bestN->hit = true;
      bool perf = bd < 55;
      score += (perf ? 300 : 100) * (1 + combo / 10);
      combo++; maxCombo = std::max(maxCombo, combo);
      if (perf) perfect++; else good++;
      judge = perf ? "PERFECT" : "GOOD"; judgeT = millis();
      Audio::blip(440.f * powf(2.f, (scale[(lane * 2 + (int)bestN->beat) % 8] - 69) / 12.f), 120, Audio::W_SAW, 0.4f);
    } else {
      combo = 0;
      Audio::blip(120, 40, Audio::W_NOISE, 0.2f);
    }
    return true;
  }
  void tick() override {
    if (!running) return;
    float bn = beatNow();
    int b = (int)floorf(bn);
    if (b != lastBeat && bn >= 0) {  // backing beat
      lastBeat = b;
      Audio::blip(b % 2 ? 90 : 60, b % 2 ? 40 : 90, b % 2 ? Audio::W_NOISE : Audio::W_SINE, b % 2 ? 0.15f : 0.6f);
      if (b % 4 == 0) Audio::blip(440.f * powf(2.f, (scale[(b / 4) % 4] - 12 - 69) / 12.f), 400, Audio::W_TRI, 0.35f);
    }
    float msPerBeat = 60000.f / bpm;
    for (auto& n : notes)
      if (!n.hit && !n.missed && (bn - n.beat) * msPerBeat > 150) { n.missed = true; miss++; combo = 0; judge = "MISS"; judgeT = millis(); }
    if (bn > SONG_BEATS + 2) {
      running = false; done = true;
      if (score > best) { best = score; saveScore("rhythm", best); }
    }
  }
  void draw() override {
    background();
    statusBar("RHYTHM", T.blue);
    int lx = 60, lw = 26, hitY = 108;
    float bn = running ? beatNow() : 0;
    for (int i = 0; i < LANES; i++) {
      int x = lx + i * (lw + 4);
      bool fl = millis() - laneFlash[i] < 90;
      cv.fillRect(x, 13, lw, 112, fl ? lerpc(T.bg1, gradc(i / 3.f), 0.3f) : T.bar);
      frame(x, hitY - 3, lw, 8, gradc(i / 3.f));
      char k[2] = {(char)toupper(keys[i]), 0};
      textC(x + lw / 2, 116, k, fl ? T.white : T.grey);
    }
    if (running)
      for (auto& n : notes) {
        if (n.hit || n.missed) continue;
        float dy = (n.beat - bn) * 34;
        int y = hitY - (int)dy;
        if (y < 10 || y > 125) continue;
        int x = lx + n.lane * (lw + 4);
        uint16_t c = gradc(n.lane / 3.f);
        cv.fillRect(x + 2, y - 2, lw - 4, 5, c);
        cv.drawFastHLine(x + 2, y - 2, lw - 4, T.white);
      }
    char b[24];
    snprintf(b, sizeof(b), "%d", score); text(4, 16, b, T.white, 1);
    snprintf(b, sizeof(b), "X%d", combo); text(4, 28, b, combo >= 10 ? T.yel : T.cyan, 2);
    if (millis() - judgeT < 400) textC(204, 50, judge.c_str(), judge == "MISS" ? T.red : (judge == "PERFECT" ? T.yel : T.green));
    if (running && bn < 0) textC(120, 50, "GET READY", T.white, 2);
    if (!running) {
      panel(30, 30, 180, 70, done ? T.yel : T.cyan, T.bg1);
      if (done) {
        textC(120, 36, "SONG CLEAR!", T.yel, 2);
        snprintf(b, sizeof(b), "PERFECT %d  GOOD %d", perfect, good); textC(120, 58, b, T.white, 1, false);
        snprintf(b, sizeof(b), "MISS %d  MAX COMBO %d", miss, maxCombo); textC(120, 70, b, T.grey, 1, false);
        snprintf(b, sizeof(b), "BEST %d", best); textC(120, 84, b, T.yel, 1, false);
      } else {
        textC(120, 38, "RHYTHM", T.cyan, 2);
        textC(120, 60, "HIT D F J K ON THE LINE", T.white, 1, false);
        snprintf(b, sizeof(b), "BEST %d", best); textC(120, 72, b, T.yel, 1, false);
        textC(120, 86, "OK TO START", T.grey, 1, false);
      }
    }
  }
};

App* appBlocks = new BlocksApp();
App* appBreakout = new BreakoutApp();
App* appFlappy = new FlappyApp();
App* appTyping = new TypingApp();
App* appRhythm = new RhythmApp();
