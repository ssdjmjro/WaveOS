// WAVE OS - games: Snake + 2048 (high scores on SD)
#include "common.h"
#include "audio.h"

int loadScore(const char* key) {
  File f = SD.open("/waveos/scores.txt");
  if (!f) return 0;
  int v = 0;
  String k = String(key) + "=";
  while (f.available()) {
    String l = f.readStringUntil('\n');
    if (l.startsWith(k)) v = l.substring(k.length()).toInt();
  }
  f.close();
  return v;
}
void saveScore(const char* key, int val) {
  if (!sdOK) return;
  std::vector<String> lines;
  File f = SD.open("/waveos/scores.txt");
  String k = String(key) + "=";
  if (f) {
    while (f.available()) { String l = f.readStringUntil('\n'); l.trim(); if (l.length() && !l.startsWith(k)) lines.push_back(l); }
    f.close();
  }
  lines.push_back(k + String(val));
  f = SD.open("/waveos/scores.txt", FILE_WRITE);
  if (!f) return;
  for (auto& l : lines) f.println(l);
  f.close();
}
static KeyCode dirKey(const KeyEv& e) {
  KeyCode k = e.nav();
  if (k != K_CHAR && k != K_NONE) return k;
  if (e.code != K_CHAR) return K_NONE;
  switch (tolower(e.ch)) { case 'w': return K_UP; case 's': return K_DOWN; case 'a': return K_LEFT; case 'd': return K_RIGHT; }
  return K_NONE;
}

// ===================================================================== SNAKE
struct SnakeApp : App {
  static constexpr int CW = 24, CH = 17, CS = 6, GX = 6, GY = 16;
  struct Pt { int8_t x, y; };
  std::vector<Pt> body;
  Pt food;
  int dx = 1, dy = 0, ndx = 1, ndy = 0;
  int score = 0, best = 0;
  bool over = false, paused = false, started = false;
  uint32_t lastStep = 0;
  int stepMs = 140;
  SnakeApp() { name = "SNAKE"; }
  void enter() override { best = loadScore("snake"); reset(); }
  void reset() {
    body.clear();
    for (int i = 0; i < 4; i++) body.push_back({(int8_t)(6 + i), 8});
    dx = ndx = 1; dy = ndy = 0;
    score = 0; over = false; paused = false; started = false; stepMs = 140;
    placeFood();
  }
  void placeFood() {
    for (;;) {
      Pt p{(int8_t)random(CW), (int8_t)random(CH)};
      bool hit = false;
      for (auto& b : body) if (b.x == p.x && b.y == p.y) hit = true;
      if (!hit) { food = p; return; }
    }
  }
  bool key(const KeyEv& e) override {
    if (over) { if (e.code == K_ENTER || (e.code == K_CHAR && e.ch == ' ')) reset(); return e.code != K_ESC; }
    if (e.code == K_CHAR && e.ch == ' ') { paused = !paused; return true; }
    KeyCode k = dirKey(e);
    if (k == K_UP && dy == 0) { ndx = 0; ndy = -1; }
    else if (k == K_DOWN && dy == 0) { ndx = 0; ndy = 1; }
    else if (k == K_LEFT && dx == 0) { ndx = -1; ndy = 0; }
    else if (k == K_RIGHT && dx == 0) { ndx = 1; ndy = 0; }
    else return false;
    started = true;
    return true;
  }
  void tick() override {
    if (over || paused || !started || millis() - lastStep < (uint32_t)stepMs) return;
    lastStep = millis();
    dx = ndx; dy = ndy;
    Pt h = body.back();
    h.x += dx; h.y += dy;
    bool dead = h.x < 0 || h.y < 0 || h.x >= CW || h.y >= CH;
    for (size_t i = 1; i < body.size() && !dead; i++) if (body[i].x == h.x && body[i].y == h.y) dead = true;
    if (dead) {
      over = true;
      Audio::blip(196, 120, Audio::W_SQUARE); Audio::blip(130, 300, Audio::W_SAW);
      if (score > best) { best = score; saveScore("snake", best); }
      return;
    }
    body.push_back(h);
    if (h.x == food.x && h.y == food.y) {
      score += 10;
      stepMs = std::max(55, stepMs - 4);
      Audio::blip(880 + score * 2, 50, Audio::W_SQUARE);
      placeFood();
    } else {
      body.erase(body.begin());
    }
  }
  void draw() override {
    background();
    statusBar("SNAKE", T.blue);
    panel(GX - 2, GY - 2, CW * CS + 4, CH * CS + 4, T.blueD, T.bg0);
    for (int i = 0; i < CW; i++)
      for (int j = 0; j < CH; j++)
        if ((i + j) % 2 == 0) cv.fillRect(GX + i * CS, GY + j * CS, CS, CS, T.bar);
    for (size_t k = 0; k < body.size(); k++) {
      uint16_t c = gradc(1.f - (float)k / body.size());
      int x = GX + body[k].x * CS, y = GY + body[k].y * CS;
      cv.fillRect(x, y, CS - 1, CS - 1, c);
      cv.drawFastHLine(x, y, CS - 1, lerpc(c, T.white, 0.4f));
    }
    Pt h = body.back();
    cv.drawPixel(GX + h.x * CS + 2 + dx, GY + h.y * CS + 1 + (dy > 0 ? 2 : 0), T.bg0);
    cv.drawPixel(GX + h.x * CS + 2 + dx, GY + h.y * CS + 3 + (dy < 0 ? -2 : 0), T.bg0);
    int fx = GX + food.x * CS, fy = GY + food.y * CS;
    bool blink = (millis() / 300) % 2;
    cv.fillRect(fx + 1, fy, 3, 5, T.pink); cv.fillRect(fx, fy + 1, 5, 3, T.pink);
    if (blink) cv.drawPixel(fx + 1, fy + 1, T.white);
    int sx = 158;
    char b[16];
    panel(sx, 15, 78, 40, T.blueD);
    text(sx + 5, 19, "SCORE", T.grey);
    snprintf(b, sizeof(b), "%04d", score);
    text(sx + 5, 31, b, T.white, 2);
    panel(sx, 58, 78, 22, T.blueD);
    text(sx + 5, 62, "BEST", T.grey);
    snprintf(b, sizeof(b), "%04d", best);
    text(sx + 5, 71, b, T.yel);
    panel(sx, 83, 78, 40, T.blueD);
    text(sx + 5, 87, "LEN", T.grey, 1, false);
    snprintf(b, sizeof(b), "%d", (int)body.size());
    text(sx + 40, 87, b, T.cyan, 1, false);
    text(sx + 5, 98, "SPEED", T.grey, 1, false);
    snprintf(b, sizeof(b), "%d", (140 - stepMs) / 4 + 1);
    text(sx + 40, 98, b, T.cyan, 1, false);
    if (!started && !over) textC(GX + CW * CS / 2, 60, "ARROWS TO START", T.white);
    if (paused) textC(GX + CW * CS / 2, 60, "PAUSED", T.white, 2);
    if (over) {
      panel(20, 42, 120, 44, T.red, T.bg1);
      textC(80, 48, "GAME OVER", T.red, 2);
      textC(80, 68, score >= best && score ? "NEW BEST!" : "OK = RETRY", score >= best && score ? T.yel : T.grey);
    }
    hintBar({{"<>^v", "MOVE"}, {"WASD", ""}, {"SPC", "PAUSE"}, {"ESC", "BACK"}});
  }
};

// ===================================================================== 2048
struct G2048App : App {
  int g[4][4], ug[4][4];
  int score = 0, uscore = 0, best = 0;
  bool over = false, won = false, keepGoing = false, canUndo = false;
  uint32_t popT = 0;
  int popX = -1, popY = -1;
  G2048App() { name = "MERGE"; }
  void enter() override { best = loadScore("2048"); if (!score && !over) reset(); }
  void reset() {
    memset(g, 0, sizeof(g));
    score = 0; over = false; won = false; keepGoing = false; canUndo = false;
    spawn(); spawn();
  }
  void spawn() {
    int empty = 0;
    for (auto& r : g) for (int v : r) if (!v) empty++;
    if (!empty) return;
    int k = random(empty);
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) if (!g[y][x] && k-- == 0) { g[y][x] = random(10) ? 2 : 4; popX = x; popY = y; popT = millis(); }
  }
  bool slide(int dx, int dy) {
    bool moved = false;
    for (int line = 0; line < 4; line++) {
      int vals[4], n = 0;
      for (int i = 0; i < 4; i++) {
        int idx = (dx > 0 || dy > 0) ? 3 - i : i;
        int x = dx ? idx : line, y = dx ? line : idx;
        if (g[y][x]) vals[n++] = g[y][x];
      }
      int out[4] = {0, 0, 0, 0}, m = 0;
      for (int i = 0; i < n; i++) {
        if (i + 1 < n && vals[i] == vals[i + 1]) { out[m++] = vals[i] * 2; score += vals[i] * 2; if (vals[i] * 2 == 2048) won = true; i++; }
        else out[m++] = vals[i];
      }
      for (int i = 0; i < 4; i++) {
        int idx = (dx > 0 || dy > 0) ? 3 - i : i;
        int x = dx ? idx : line, y = dx ? line : idx;
        if (g[y][x] != out[i]) moved = true;
        g[y][x] = out[i];
      }
    }
    return moved;
  }
  bool anyMove() {
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
      if (!g[y][x]) return true;
      if (x < 3 && g[y][x] == g[y][x + 1]) return true;
      if (y < 3 && g[y][x] == g[y + 1][x]) return true;
    }
    return false;
  }
  bool key(const KeyEv& e) override {
    if (e.is('u') && canUndo) { memcpy(g, ug, sizeof(g)); score = uscore; over = false; canUndo = false; return true; }
    if (e.is('n')) { reset(); return true; }
    if (over || (won && !keepGoing)) {
      if (e.code == K_ENTER) { if (won && !over) keepGoing = true; else reset(); return true; }
      return false;
    }
    KeyCode k = dirKey(e);
    int dx = 0, dy = 0;
    if (k == K_UP) dy = -1; else if (k == K_DOWN) dy = 1; else if (k == K_LEFT) dx = -1; else if (k == K_RIGHT) dx = 1; else return false;
    int tg[4][4]; memcpy(tg, g, sizeof(g));
    int ts = score;
    if (slide(dx, dy)) {
      memcpy(ug, tg, sizeof(g)); uscore = ts; canUndo = true;
      spawn();
      Audio::blip(score > ts ? 660 : 330, 30, Audio::W_TRI);
      if (score > best) { best = score; }
      if (!anyMove()) { over = true; saveScore("2048", best); Audio::blip(150, 300, Audio::W_SAW); }
    }
    return true;
  }
  uint16_t tileCol(int v) {
    int l = 0; while (v > 1) { v >>= 1; l++; }
    if (l <= 0) return T.bg2;
    return l >= 11 ? T.yel : gradc((l - 1) / 10.f);
  }
  void draw() override {
    background();
    statusBar("MERGE", T.blue);
    int ts = 26, gx = 8, gy = 16;
    panel(gx - 3, gy - 3, 4 * (ts + 2) + 4, 4 * (ts + 2) + 4, T.blueD, T.bg0);
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
      int v = g[y][x];
      int px = gx + x * (ts + 2), py = gy + y * (ts + 2);
      int inset = (x == popX && y == popY && millis() - popT < 120) ? 5 : 0;
      if (!v) { cv.fillRect(px, py, ts, ts, T.bar); continue; }
      uint16_t c = tileCol(v);
      cv.fillRect(px + inset, py + inset, ts - inset * 2, ts - inset * 2, c);
      cv.drawFastHLine(px + inset, py + inset, ts - inset * 2, lerpc(c, T.white, 0.4f));
      cv.drawFastHLine(px + inset, py + ts - 1 - inset, ts - inset * 2, lerpc(c, T.bg0, 0.4f));
      char b[8]; snprintf(b, sizeof(b), "%d", v);
      textC(px + ts / 2, py + ts / 2 - 3, b, v >= 8 ? T.white : T.bg0, 1, v >= 8);
    }
    int sx = 128;
    char b[16];
    panel(sx, 15, 108, 34, T.blueD);
    text(sx + 6, 19, "SCORE", T.grey);
    snprintf(b, sizeof(b), "%d", score);
    text(sx + 6, 30, b, T.white, 2);
    panel(sx, 52, 108, 22, T.blueD);
    text(sx + 6, 56, "BEST", T.grey);
    snprintf(b, sizeof(b), "%d", best);
    text(sx + 40, 56, b, T.yel);
    panel(sx, 77, 108, 46, T.blueD);
    text(sx + 6, 81, "ARROWS/WASD", T.grey, 1, false);
    text(sx + 6, 92, "U  UNDO", canUndo ? T.cyan : T.dim, 1, false);
    text(sx + 6, 103, "N  NEW GAME", T.grey, 1, false);
    if (over || (won && !keepGoing)) {
      panel(14, 46, 110, 40, over ? T.red : T.yel, T.bg1);
      textC(69, 52, over ? "NO MOVES" : "2048 TILE!", over ? T.red : T.yel, 2);
      textC(69, 72, over ? "OK = NEW GAME" : "OK = KEEP GOING", T.grey, 1, false);
    }
    hintBar({{"<>^v", "SLIDE"}, {"U", "UNDO"}, {"N", "NEW"}, {"ESC", "BACK"}});
  }
};

App* appSnake = new SnakeApp();
App* app2048 = new G2048App();
