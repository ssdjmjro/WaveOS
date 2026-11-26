// WAVE OS - games pack 4: MAZE, FOUR (vs CPU), TANKS (artillery vs CPU), ROCKS (space rocks), HOPPER (road + river)
#include "common.h"
#include "audio.h"
#include <algorithm>

static bool held4(char c) { for (char h : heldKeys) if (h == c) return true; return false; }
static KeyCode dir4(const KeyEv& e) {
  KeyCode k = e.nav();
  if (k >= K_UP && k <= K_RIGHT) return k;
  if (e.code != K_CHAR) return K_NONE;
  switch (tolower(e.ch)) { case 'w': return K_UP; case 's': return K_DOWN; case 'a': return K_LEFT; case 'd': return K_RIGHT; }
  return K_NONE;
}
static void over4(int cx, const char* t, const char* sub, uint16_t c) {
  int w = std::max(textW(t, 2), textW(sub)) + 20;
  panel(cx - w / 2, 46, w, 40, c, T.bg1);
  textC(cx, 52, t, c, 2);
  textC(cx, 72, sub, T.grey, 1, false);
}

// ===================================================================== MAZE
struct MazeApp : App {
  static constexpr int W = 23, H = 11, CS = 10, OX = 5, OY = 14;
  uint8_t m[H][W];  // 1 = wall
  int px, py, ex, ey, level = 1, best = 0;
  uint32_t t0 = 0;
  bool won = false;
  MazeApp() { name = "MAZE"; }
  void gen() {
    memset(m, 1, sizeof(m));
    std::vector<std::pair<int, int>> st{{1, 1}};
    m[1][1] = 0;
    while (!st.empty()) {
      auto [x, y] = st.back();
      int dirs[4][2] = {{2, 0}, {-2, 0}, {0, 2}, {0, -2}};
      for (int i = 3; i > 0; i--) { int j = random(i + 1); std::swap(dirs[i][0], dirs[j][0]); std::swap(dirs[i][1], dirs[j][1]); }
      bool moved = false;
      for (auto& d : dirs) {
        int nx = x + d[0], ny = y + d[1];
        if (nx > 0 && ny > 0 && nx < W - 1 && ny < H - 1 && m[ny][nx]) { m[y + d[1] / 2][x + d[0] / 2] = 0; m[ny][nx] = 0; st.push_back({nx, ny}); moved = true; break; }
      }
      if (!moved) st.pop_back();
    }
    for (int k = 0; k < level && k < 6; k++) { int x = 1 + random(W - 2), y = 1 + random(H - 2); m[y][x] = 0; }  // a few loops
    px = 1; py = 1; ex = W - 2; ey = H - 2; won = false; t0 = millis();
  }
  void enter() override { best = loadScore("maze"); level = 1; gen(); }
  bool fog() { return level >= 3; }
  bool key(const KeyEv& e) override {
    if (won) { if (e.code == K_ENTER || (e.code == K_CHAR && e.ch == ' ')) { level++; gen(); } return e.code != K_ESC; }
    KeyCode k = dir4(e);
    int nx = px, ny = py;
    if (k == K_UP) ny--; else if (k == K_DOWN) ny++; else if (k == K_LEFT) nx--; else if (k == K_RIGHT) nx++; else return false;
    if (!m[ny][nx]) { px = nx; py = ny; Audio::blip(500 + (px + py) * 10, 10, Audio::W_TRI, 0.12f); }
    if (px == ex && py == ey) {
      won = true; Audio::blip(1320, 200, Audio::W_SQUARE, 0.4f);
      if (level > best) { best = level; saveScore("maze", best); }
    }
    return true;
  }
  void draw() override {
    background(true);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
      int d2 = (x - px) * (x - px) + (y - py) * (y - py);
      if (fog() && d2 > 10 && !(x == ex && y == ey)) continue;
      int sx = OX + x * CS, sy = OY + y * CS;
      if (m[y][x]) { uint16_t c = gradc((x + y) / (float)(W + H)); cv.fillRect(sx, sy, CS, CS, lerpc(T.bg1, c, 0.55f)); cv.drawFastHLine(sx, sy, CS, lerpc(T.bg1, c, 0.8f)); }
    }
    cv.fillRect(OX + ex * CS + 2, OY + ey * CS + 2, CS - 4, CS - 4, (millis() / 300) % 2 ? T.yel : T.green);
    cv.fillCircle(OX + px * CS + CS / 2, OY + py * CS + CS / 2, 3, T.white);
    statusBar("MAZE", T.blue);
    char b[32];
    snprintf(b, sizeof(b), "LV %d  %us  BEST %d", level, (unsigned)((millis() - t0) / 1000), best); text(70, 2, b, T.white, 1, false);
    if (won) over4(120, "ESCAPED!", "OK = NEXT MAZE", T.green);
    hintBar({{"ARROWS", "MOVE"}, {"FOG", "FROM LV3"}, {"ESC", "BACK"}});
  }
};

// ===================================================================== FOUR (four in a row vs CPU)
struct FourApp : App {
  int8_t b[6][7];  // 0 empty, 1 you, 2 cpu
  int col = 3, wins = 0, losses = 0;
  int state = 0;  // 0 your turn, 1 cpu thinking, 2 win, 3 lose, 4 draw
  uint32_t cpuT = 0;
  int lastR = -1, lastC = -1;
  FourApp() { name = "FOUR"; }
  void reset() { memset(b, 0, sizeof(b)); state = 0; lastR = lastC = -1; }
  void enter() override { reset(); }
  int drop(int c, int who) { for (int r = 5; r >= 0; r--) if (!b[r][c]) { b[r][c] = who; return r; } return -1; }
  void undrop(int c) { for (int r = 0; r < 6; r++) if (b[r][c]) { b[r][c] = 0; return; } }
  bool winAt(int who) {
    for (int r = 0; r < 6; r++) for (int c = 0; c < 7; c++) {
      if (b[r][c] != who) continue;
      const int d[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};
      for (auto& dd : d) {
        int k = 1;
        while (k < 4) { int rr = r + dd[0] * k, cc = c + dd[1] * k; if (rr < 0 || rr >= 6 || cc < 0 || cc >= 7 || b[rr][cc] != who) break; k++; }
        if (k == 4) return true;
      }
    }
    return false;
  }
  int scoreWin(int a, int c, int e) { return a == 4 ? 1000 : a == 3 && e == 1 ? 6 : a == 2 && e == 2 ? 2 : 0; (void)c; }
  int heur() {
    int s = 0;
    for (int r = 0; r < 6; r++) if (b[r][3] == 2) s += 3; else if (b[r][3] == 1) s -= 3;
    const int d[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};
    for (int r = 0; r < 6; r++) for (int c = 0; c < 7; c++) for (auto& dd : d) {
      int er = r + dd[0] * 3, ec = c + dd[1] * 3;
      if (er < 0 || er >= 6 || ec < 0 || ec >= 7) continue;
      int cp = 0, pl = 0, em = 0;
      for (int k = 0; k < 4; k++) { int v = b[r + dd[0] * k][c + dd[1] * k]; if (v == 2) cp++; else if (v == 1) pl++; else em++; }
      if (!pl) s += cp == 3 ? 8 : cp == 2 ? 2 : 0;
      if (!cp) s -= pl == 3 ? 10 : pl == 2 ? 2 : 0;
    }
    return s;
  }
  int minimax(int depth, bool cpu, int alpha, int beta) {
    if (winAt(2)) return 100000 + depth;
    if (winAt(1)) return -100000 - depth;
    if (!depth) return heur();
    bool any = false;
    int best = cpu ? INT32_MIN : INT32_MAX;
    static const int order[7] = {3, 2, 4, 1, 5, 0, 6};
    for (int c : order) {
      if (b[0][c]) continue;
      any = true;
      drop(c, cpu ? 2 : 1);
      int v = minimax(depth - 1, !cpu, alpha, beta);
      undrop(c);
      if (cpu) { best = std::max(best, v); alpha = std::max(alpha, v); } else { best = std::min(best, v); beta = std::min(beta, v); }
      if (beta <= alpha) break;
    }
    return any ? best : 0;
  }
  void cpuMove() {
    int bestC = 3, bestV = INT32_MIN;
    static const int order[7] = {3, 2, 4, 1, 5, 0, 6};
    for (int c : order) {
      if (b[0][c]) continue;
      drop(c, 2);
      int v = minimax(4, false, INT32_MIN, INT32_MAX) + random(3);
      undrop(c);
      if (v > bestV) { bestV = v; bestC = c; }
    }
    lastR = drop(bestC, 2); lastC = bestC;
    Audio::blip(330, 60, Audio::W_TRI, 0.3f);
  }
  bool full() { for (int c = 0; c < 7; c++) if (!b[0][c]) return false; return true; }
  bool key(const KeyEv& e) override {
    if (state >= 2) { if (e.code == K_ENTER) { reset(); return true; } return e.code != K_ESC; }
    if (state != 0) return e.code != K_ESC;
    KeyCode k = dir4(e);
    if (k == K_LEFT) { col = (col + 6) % 7; return true; }
    if (k == K_RIGHT) { col = (col + 1) % 7; return true; }
    if (e.code == K_ENTER || (e.code == K_CHAR && e.ch == ' ') || k == K_DOWN) {
      int r = drop(col, 1);
      if (r < 0) return true;
      lastR = r; lastC = col;
      Audio::blip(523, 60, Audio::W_SQUARE, 0.3f);
      if (winAt(1)) { state = 2; wins++; Audio::blip(1046, 300, Audio::W_SQUARE, 0.4f); }
      else if (full()) state = 4;
      else { state = 1; cpuT = millis() + 350; }
      return true;
    }
    return false;
  }
  void tick() override {
    if (state == 1 && millis() > cpuT) {
      cpuMove();
      if (winAt(2)) { state = 3; losses++; Audio::blip(150, 400, Audio::W_SAW, 0.4f); }
      else if (full()) state = 4;
      else state = 0;
    }
  }
  void draw() override {
    background();
    statusBar("FOUR", T.blue);
    int cs = 15, ox = 120 - 7 * cs / 2, oy = 24;
    panel(ox - 4, oy - 3, 7 * cs + 8, 6 * cs + 6, T.blueD, T.blueD);
    for (int r = 0; r < 6; r++) for (int c = 0; c < 7; c++) {
      int x = ox + c * cs + cs / 2, y = oy + r * cs + cs / 2;
      uint16_t col2 = b[r][c] == 1 ? T.yel : b[r][c] == 2 ? T.pink : T.bg0;
      cv.fillCircle(x, y, 6, col2);
      if (b[r][c]) cv.drawPixel(x - 2, y - 2, T.white);
      if (r == lastR && c == lastC) cv.drawCircle(x, y, 7, T.white);
    }
    if (state == 0) cv.fillTriangle(ox + col * cs + 3, 14, ox + col * cs + cs - 3, 14, ox + col * cs + cs / 2, 20, T.yel);
    char bb[24];
    snprintf(bb, sizeof(bb), "YOU %d", wins); text(6, 30, bb, T.yel);
    snprintf(bb, sizeof(bb), "CPU %d", losses); text(6, 42, bb, T.pink);
    if (state == 1) text(190, 30, "HMM...", T.grey, 1, false);
    if (state == 2) over4(120, "YOU WIN!", "OK = AGAIN", T.green);
    if (state == 3) over4(120, "CPU WINS", "OK = AGAIN", T.red);
    if (state == 4) over4(120, "DRAW", "OK = AGAIN", T.grey);
    hintBar({{"<>", "COLUMN"}, {"OK", "DROP"}, {"ESC", "BACK"}});
  }
};

// ===================================================================== TANKS (artillery vs CPU)
struct TanksApp : App {
  int ground[240];
  float ang[2] = {45, 135}, pow_[2] = {60, 60};
  int hp[2] = {3, 3}, tx[2], turn = 0, wins = 0;
  float wind = 0;
  bool flying = false, over = false;
  float bx, by, bvx, bvy;
  uint32_t cpuT = 0;
  struct Px { float x, y, vx, vy; int l; };
  std::vector<Px> fx;
  TanksApp() { name = "TANKS"; }
  void terrain() {
    float a = random(20, 50) / 1000.f, b = random(10, 30) / 1000.f, p1 = random(628) / 100.f, p2 = random(628) / 100.f;
    for (int x = 0; x < 240; x++) ground[x] = 95 - (int)(14 * sinf(x * a + p1) + 8 * sinf(x * b + p2));
    tx[0] = random(14, 60); tx[1] = random(180, 226);
    for (int k = 0; k < 2; k++) for (int x = tx[k] - 8; x <= tx[k] + 8; x++) ground[constrain(x, 0, 239)] = ground[tx[k]];
  }
  void reset() { hp[0] = hp[1] = 3; terrain(); turn = 0; wind = random(-20, 21) / 10.f; over = flying = false; }
  void enter() override { reset(); }
  void fire() {
    int k = turn;
    float a = ang[k] * PI / 180;
    bx = tx[k] + cosf(a) * 8; by = ground[tx[k]] - 6 - sinf(a) * 8;
    bvx = cosf(a) * pow_[k] / 20.f; bvy = -sinf(a) * pow_[k] / 20.f;
    flying = true;
    Audio::blip(180, 120, Audio::W_NOISE, 0.4f);
  }
  bool key(const KeyEv& e) override {
    if (over) { if (e.code == K_ENTER) { reset(); return true; } return e.code != K_ESC; }
    if (flying || turn != 0) return e.code != K_ESC;
    KeyCode k = dir4(e);
    if (k == K_UP) { ang[0] = std::min(170.f, ang[0] + (e.rep ? 3 : 1)); return true; }
    if (k == K_DOWN) { ang[0] = std::max(5.f, ang[0] - (e.rep ? 3 : 1)); return true; }
    if (k == K_RIGHT) { pow_[0] = std::min(100.f, pow_[0] + (e.rep ? 3 : 1)); return true; }
    if (k == K_LEFT) { pow_[0] = std::max(10.f, pow_[0] - (e.rep ? 3 : 1)); return true; }
    if ((e.code == K_CHAR && e.ch == ' ') || e.code == K_ENTER) { fire(); return true; }
    return false;
  }
  void cpuAim() {  // simulate shots, pick the closest, then add some error
    float bestA = 135, bestP = 60, bestD = 1e9;
    for (int a = 95; a <= 170; a += 5) for (int p = 30; p <= 100; p += 5) {
      float x = tx[1] + cosf(a * PI / 180) * 8, y = ground[tx[1]] - 6 - sinf(a * PI / 180) * 8, vx = cosf(a * PI / 180) * p / 20.f, vy = -sinf(a * PI / 180) * p / 20.f;
      for (int s = 0; s < 400; s++) {
        x += vx; y += vy; vy += 0.05f; vx += wind * 0.002f;
        if (x < 0 || x >= 240) break;
        if (y >= ground[(int)x]) { float d = fabsf(x - tx[0]); if (d < bestD) { bestD = d; bestA = a; bestP = p; } break; }
      }
    }
    ang[1] = bestA + random(-6, 7); pow_[1] = bestP + random(-6, 7);
  }
  void explode(int cx, int cy) {
    for (int x = cx - 10; x <= cx + 10; x++) {
      if (x < 0 || x >= 240) continue;
      int dy = (int)sqrtf(std::max(0, 100 - (x - cx) * (x - cx)));
      if (ground[x] < cy + dy) ground[x] = std::min(124, std::max(ground[x], cy + dy));
    }
    for (int i = 0; i < 20; i++) { float a = random(628) / 100.f, v = random(5, 25) / 10.f; fx.push_back({(float)cx, (float)cy, cosf(a) * v, sinf(a) * v - 1, (int)random(10, 25)}); }
    for (int k = 0; k < 2; k++)
      if (abs(tx[k] - cx) < 13 && abs(ground[tx[k]] - cy) < 16) { hp[k]--; Audio::blip(80, 400, Audio::W_NOISE, 0.6f); }
    Audio::blip(120, 250, Audio::W_NOISE, 0.5f);
    if (hp[0] <= 0 || hp[1] <= 0) { over = true; if (hp[1] <= 0) wins++; }
  }
  void tick() override {
    for (size_t i = 0; i < fx.size(); i++) { fx[i].x += fx[i].vx; fx[i].y += fx[i].vy; fx[i].vy += 0.08f; if (--fx[i].l <= 0) { fx.erase(fx.begin() + i); i--; } }
    if (over) return;
    if (!flying && turn == 1 && millis() > cpuT) { cpuAim(); fire(); }
    if (!flying) return;
    for (int s = 0; s < 3; s++) {
      bx += bvx; by += bvy; bvy += 0.05f; bvx += wind * 0.002f;
      if (bx < 0 || bx >= 240 || by > 125) { flying = false; break; }
      if (by >= ground[(int)bx]) { explode((int)bx, (int)by); flying = false; break; }
    }
    if (!flying && !over) { turn = 1 - turn; wind = constrain(wind + random(-5, 6) / 10.f, -2.f, 2.f); if (turn == 1) cpuT = millis() + 700; }
  }
  void drawTank(int k, uint16_t c) {
    int x = tx[k], y = ground[x];
    cv.fillRoundRect(x - 7, y - 6, 14, 6, 2, c);
    cv.fillRect(x - 4, y - 9, 8, 3, c);
    float a = ang[k] * PI / 180;
    cv.drawLine(x, y - 8, x + cosf(a) * 9, y - 8 - sinf(a) * 9, T.white);
    for (int i = 0; i < hp[k]; i++) cv.fillRect(x - 5 + i * 4, y - 14, 3, 2, c);
  }
  void draw() override {
    background();
    for (int x = 0; x < 240; x++) { cv.drawFastVLine(x, ground[x], 125 - ground[x], lerpc(T.bg1, T.green, 0.35f)); cv.drawPixel(x, ground[x], T.green); }
    drawTank(0, T.cyan); drawTank(1, T.pink);
    if (flying) cv.fillCircle(bx, by, 2, T.yel);
    for (auto& p : fx) cv.fillRect(p.x, p.y, 2, 2, lerpc(T.bg0, T.yel, p.l / 25.f));
    statusBar("TANKS", T.blue);
    char b[40];
    snprintf(b, sizeof(b), "ANG %d  POW %d", (int)ang[0], (int)pow_[0]); text(6, 16, b, turn == 0 ? T.cyan : T.dim, 1, false);
    snprintf(b, sizeof(b), "WIND %s%.1f", wind < 0 ? "<" : ">", fabsf(wind)); text(150, 16, b, T.grey, 1, false);
    if (turn == 1 && !over) text(150, 26, "CPU AIMING", T.pink, 1, false);
    if (over) over4(120, hp[1] <= 0 ? "VICTORY!" : "DESTROYED", "OK = AGAIN", hp[1] <= 0 ? T.green : T.red);
    hintBar({{"^v", "ANGLE"}, {"<>", "POWER"}, {"SPC", "FIRE"}, {"ESC", "BACK"}});
  }
};

// ===================================================================== ROCKS (space rocks)
struct RocksApp : App {
  float sx = 120, sy = 70, sa = -PI / 2, vx = 0, vy = 0;
  struct Rock { float x, y, vx, vy; int size; float rot; uint8_t shape[8]; };
  struct Shot { float x, y, vx, vy; int life; };
  std::vector<Rock> rocks;
  std::vector<Shot> shots;
  struct Bit { float x, y, vx, vy; uint8_t life; uint16_t c; };
  std::vector<Bit> bits;  // explosion debris
  int score = 0, best = 0, lives = 3, level = 0;
  uint32_t lastShot = 0, safeT = 0, waveT = 0, hyperT = 0;
  void boom(float x, float y, int n, uint16_t c) {
    for (int i = 0; i < n && bits.size() < 70; i++) {
      float a = random(628) / 100.f, v = random(5, 22) / 10.f;
      bits.push_back({x, y, cosf(a) * v, sinf(a) * v, (uint8_t)random(14, 30), c});
    }
  }
  bool over = false;
  RocksApp() { name = "ROCKS"; }
  void addRock(float x, float y, int size) {
    float sp = (1.2f / size + 0.3f) * (1 + std::min(level, 10) * 0.08f);  // a little faster every wave
    Rock r{x, y, (random(-10, 11)) / 10.f * sp, (random(-10, 11)) / 10.f * sp, size, 0, {}};
    for (auto& s : r.shape) s = random(70, 100);
    rocks.push_back(r);
  }
  void nextLevel() { level++; for (int i = 0; i < 2 + level; i++) { float x = random(240), y = random(14, 125); if (fabsf(x - sx) < 50) x = fmodf(x + 120, 240); addRock(x, y, 3); } safeT = millis(); waveT = millis(); }
  void reset() { rocks.clear(); shots.clear(); bits.clear(); score = 0; lives = 3; level = 0; over = false; sx = 120; sy = 70; vx = vy = 0; nextLevel(); }
  void enter() override { best = loadScore("rocks"); if (over || !score) reset(); }
  void leave() override { bits.clear(); bits.shrink_to_fit(); }
  bool key(const KeyEv& e) override {
    if (over) { if (e.code == K_ENTER) { reset(); return true; } return false; }
    return dir4(e) != K_NONE || (e.code == K_CHAR && e.ch == ' ');
  }
  static float wrapX(float x) { return x < 0 ? x + 240 : x >= 240 ? x - 240 : x; }
  static float wrapY(float y) { return y < 12 ? y + 113 : y >= 125 ? y - 113 : y; }
  void tick() override {
    if (over) return;
    if (held4(',') || held4('a')) sa -= 0.09f;
    if (held4('/') || held4('d')) sa += 0.09f;
    if (held4(';') || held4('w')) { vx += cosf(sa) * 0.08f; vy += sinf(sa) * 0.08f; }
    if ((held4('.') || held4('s')) && millis() - hyperT > 1500) {  // hyperspace: jump somewhere random
      hyperT = millis(); boom(sx, sy, 8, T.cyan);
      sx = random(10, 230); sy = random(20, 118); vx = vy = 0;
      Audio::blip(1800, 80, Audio::W_SINE, 0.3f);
    }
    for (size_t i = 0; i < bits.size(); i++) {
      Bit& b = bits[i]; b.x += b.vx; b.y += b.vy; b.vx *= 0.96f; b.vy *= 0.96f;
      if (!--b.life) { bits[i] = bits.back(); bits.pop_back(); i--; }
    }
    vx *= 0.99f; vy *= 0.99f;
    sx = wrapX(sx + vx); sy = wrapY(sy + vy);
    if (held4(' ') && millis() - lastShot > 200 && shots.size() < 6) {
      lastShot = millis();
      shots.push_back({sx + cosf(sa) * 8, sy + sinf(sa) * 8, cosf(sa) * 4 + vx, sinf(sa) * 4 + vy, 45});
      Audio::blip(1400, 15, Audio::W_SQUARE, 0.15f);
    }
    for (auto& r : rocks) { r.x = wrapX(r.x + r.vx); r.y = wrapY(r.y + r.vy); r.rot += 0.02f; }
    for (size_t i = 0; i < shots.size(); i++) {
      Shot& s = shots[i];
      s.x = wrapX(s.x + s.vx); s.y = wrapY(s.y + s.vy);
      bool dead = --s.life <= 0;
      for (size_t j = 0; j < rocks.size() && !dead; j++) {
        Rock& r = rocks[j];
        float rad = r.size * 5.f;
        if ((s.x - r.x) * (s.x - r.x) + (s.y - r.y) * (s.y - r.y) < rad * rad) {
          dead = true;
          score += (4 - r.size) * 20;
          Audio::blip(90 + r.size * 40, 120, Audio::W_NOISE, 0.4f);
          Rock old = r;
          boom(old.x, old.y, 4 + old.size * 3, old.size == 3 ? T.grey : old.size == 2 ? T.yel : T.red);
          rocks.erase(rocks.begin() + j);
          if (old.size > 1) { addRock(old.x, old.y, old.size - 1); addRock(old.x, old.y, old.size - 1); }
        }
      }
      if (dead) { shots.erase(shots.begin() + i); i--; }
    }
    if (millis() - safeT > 2000)
      for (auto& r : rocks) {
        float rad = r.size * 5.f + 4;
        if ((sx - r.x) * (sx - r.x) + (sy - r.y) * (sy - r.y) < rad * rad) {
          lives--; Audio::blip(70, 500, Audio::W_SAW, 0.5f);
          boom(sx, sy, 22, T.cyan); boom(sx, sy, 10, T.white);
          sx = 120; sy = 70; vx = vy = 0; safeT = millis();
          if (lives <= 0) { over = true; if (score > best) { best = score; saveScore("rocks", best); } }
          break;
        }
      }
    if (rocks.empty()) nextLevel();
  }
  void draw() override {
    background();
    for (auto& r : rocks) {
      float rad = r.size * 5.f;
      int px = -1, py = -1, fx0 = 0, fy0 = 0;
      for (int k = 0; k <= 8; k++) {
        float a = r.rot + k * PI / 4, rr = rad * r.shape[k % 8] / 100.f;
        int x = r.x + cosf(a) * rr, y = r.y + sinf(a) * rr;
        if (k) cv.drawLine(px, py, x, y, T.grey); else { fx0 = x; fy0 = y; }
        px = x; py = y;
      }
      (void)fx0; (void)fy0;
    }
    for (auto& s : shots) cv.fillRect(s.x - 1, s.y - 1, 2, 2, T.yel);
    for (auto& b : bits) cv.drawPixel(b.x, b.y, b.life > 8 ? b.c : lerpc(T.bg0, b.c, b.life / 8.f));
    bool blink = millis() - safeT < 2000 && (millis() / 120) % 2;
    if (!blink && !over) {
      float c = cosf(sa), s = sinf(sa);
      int x1 = sx + c * 9, y1 = sy + s * 9, x2 = sx + cosf(sa + 2.5f) * 7, y2 = sy + sinf(sa + 2.5f) * 7, x3 = sx + cosf(sa - 2.5f) * 7, y3 = sy + sinf(sa - 2.5f) * 7;
      cv.drawTriangle(x1, y1, x2, y2, x3, y3, T.cyan);
      if (held4(';') || held4('w')) cv.drawLine(sx - c * 6, sy - s * 6, sx - c * (9 + random(4)), sy - s * (9 + random(4)), T.red);
    }
    statusBar("ROCKS", T.blue);
    char b[24];
    snprintf(b, sizeof(b), "%d  LV%d", score, level); text(80, 2, b, T.white, 1, false);
    for (int i = 0; i < lives; i++) cv.fillTriangle(150 + i * 8, 9, 153 + i * 8, 2, 156 + i * 8, 9, T.cyan);
    if (!over && millis() - waveT < 1300) { snprintf(b, sizeof(b), "WAVE %d", level); textC(120, 40, b, (millis() / 150) % 2 ? T.white : T.cyan, 2); }
    if (over) { snprintf(b, sizeof(b), "BEST %d - OK AGAIN", best); over4(120, "GAME OVER", b, T.red); }
    hintBar({{"<>", "TURN"}, {"^", "THRUST"}, {"SPC", "FIRE"}, {"v", "HYPERSPACE"}});
  }
};

// ===================================================================== HOPPER (road + river crossing)
struct HopperApp : App {
  static constexpr int CS = 12, COLS = 20, ROWS = 9, OY = 14;
  struct Lane { int type; float speed; std::vector<float> items; int len; };  // type 0 safe, 1 road, 2 river, 3 goal
  Lane lanes[ROWS];
  float fx = 10, fy = ROWS - 1;
  int level = 1, score = 0, best = 0, lives = 3;
  bool over = false;
  uint32_t deadT = 0;
  HopperApp() { name = "HOPPER"; }
  void build() {
    int types[ROWS] = {3, 2, 2, 2, 0, 1, 1, 1, 0};
    for (int r = 0; r < ROWS; r++) {
      Lane& l = lanes[r];
      l.type = types[r];
      l.items.clear();
      float sp = (0.3f + random(0, 6) / 10.f + level * 0.08f) * (r % 2 ? 1 : -1);
      l.speed = l.type == 1 || l.type == 2 ? sp : 0;
      l.len = l.type == 2 ? random(3, 5) : random(1, 3);
      int n = l.type == 2 ? 3 : l.type == 1 ? 2 + level / 3 : 0;
      for (int i = 0; i < n; i++) l.items.push_back(i * (COLS * CS) / (float)n + random(0, 20));
    }
    fx = 10; fy = ROWS - 1;
  }
  void reset() { level = 1; score = 0; lives = 3; over = false; build(); }
  void enter() override { best = loadScore("hopper"); reset(); }
  void lose() {
    lives--; deadT = millis(); Audio::blip(120, 300, Audio::W_SAW, 0.4f);
    if (lives <= 0) { over = true; if (score > best) { best = score; saveScore("hopper", best); } }
    fx = 10; fy = ROWS - 1;
  }
  bool key(const KeyEv& e) override {
    if (over) { if (e.code == K_ENTER) { reset(); return true; } return false; }
    KeyCode k = dir4(e);
    if (k == K_NONE) return false;
    if (k == K_UP) fy--; else if (k == K_DOWN) fy = std::min((float)ROWS - 1, fy + 1); else if (k == K_LEFT) fx--; else fx++;
    fx = constrain(roundf(fx), 0.f, COLS - 1.f);
    Audio::blip(700, 20, Audio::W_TRI, 0.2f);
    if (fy <= 0) { score += 100 * level; level++; Audio::blip(1320, 200, Audio::W_SQUARE, 0.4f); toast("LEVEL UP!", 700); build(); }
    return true;
  }
  void tick() override {
    if (over) return;
    float W = COLS * CS;
    for (auto& l : lanes) for (auto& x : l.items) { x += l.speed; if (x > W + 40) x -= W + 80; if (x < -40 - l.len * CS) x += W + 80; }
    Lane& cur = lanes[(int)fy];
    float px = fx * CS + CS / 2;
    if (cur.type == 1) {
      for (auto x : cur.items) if (px > x - 2 && px < x + cur.len * CS + 2) { lose(); return; }
    } else if (cur.type == 2) {
      bool on = false;
      for (auto x : cur.items) if (px > x && px < x + cur.len * CS) on = true;
      if (!on) { lose(); return; }
      fx += cur.speed / CS;
      if (fx < -0.4f || fx > COLS - 0.6f) { lose(); return; }
    }
  }
  void draw() override {
    background(true);
    for (int r = 0; r < ROWS; r++) {
      Lane& l = lanes[r];
      int y = OY + r * CS;
      uint16_t bg = l.type == 0 ? lerpc(T.bg0, T.green, 0.25f) : l.type == 1 ? T.bg1 : l.type == 2 ? lerpc(T.bg0, T.blue, 0.35f) : lerpc(T.bg0, T.yel, 0.25f);
      cv.fillRect(0, y, 240, CS, bg);
      if (l.type == 1) for (int x = 0; x < 240; x += 16) cv.fillRect(x, y + CS / 2, 8, 1, T.dim);
      for (auto x : l.items) {
        if (l.type == 1) {
          uint16_t c = r % 2 ? T.pink : T.red;
          cv.fillRoundRect(x, y + 2, l.len * CS, CS - 4, 2, c);
          cv.fillRect(l.speed > 0 ? x + l.len * CS - 3 : x + 1, y + 3, 2, 2, T.yel);
        } else if (l.type == 2) {
          cv.fillRoundRect(x, y + 1, l.len * CS, CS - 2, 3, RGB(140, 90, 50));
          for (int k = 4; k < l.len * CS; k += 7) cv.drawFastVLine(x + k, y + 3, CS - 6, RGB(100, 60, 30));
        }
      }
    }
    bool blink = millis() - deadT < 600 && (millis() / 80) % 2;
    if (!blink) {
      int x = fx * CS, y = OY + (int)fy * CS;
      cv.fillRoundRect(x + 2, y + 2, CS - 4, CS - 4, 3, T.green);
      cv.fillRect(x + 3, y + 2, 2, 2, T.white); cv.fillRect(x + CS - 5, y + 2, 2, 2, T.white);
    }
    statusBar("HOPPER", T.blue);
    char b[32];
    snprintf(b, sizeof(b), "%d  LV%d  HI %d", score, level, best); text(70, 2, b, T.white, 1, false);
    for (int i = 0; i < lives; i++) cv.fillRect(170 + i * 6, 3, 4, 5, T.green);
    if (over) over4(120, "SPLAT!", "OK = AGAIN", T.red);
    hintBar({{"ARROWS", "HOP"}, {"GOAL", "TOP ROW"}, {"ESC", "BACK"}});
  }
};

App* appMaze = new MazeApp();
App* appFour = new FourApp();
App* appTanks = new TanksApp();
App* appRocks = new RocksApp();
App* appHopper = new HopperApp();
