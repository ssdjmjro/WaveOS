// WAVE OS - even more games: STARFALL (shooter), MINES, DUEL (paddles vs CPU), ECHO (memory tones), RUNNER
#include "common.h"
#include "audio.h"
#include <algorithm>

static KeyCode gdir3(const KeyEv& e) {
  KeyCode k = e.nav();
  if (k >= K_UP && k <= K_RIGHT) return k;
  if (e.code != K_CHAR) return K_NONE;
  switch (tolower(e.ch)) { case 'w': return K_UP; case 's': return K_DOWN; case 'a': return K_LEFT; case 'd': return K_RIGHT; }
  return K_NONE;
}
static bool held3(char c) { for (char h : heldKeys) if (h == c) return true; return false; }
static void over3(int cx, const char* t, const char* sub, uint16_t c) {
  int w = std::max(textW(t, 2), textW(sub)) + 20;
  panel(cx - w / 2, 46, w, 40, c, T.bg1);
  textC(cx, 52, t, c, 2);
  textC(cx, 72, sub, T.grey, 1, false);
}

// ===================================================================== STARFALL (2x pixel sprites, power-ups, boss waves)
struct StarfallApp : App {
  float sx = 120;
  struct B { float x, y, vx; bool enemy; };
  struct En { float x, y, vx; int hp, type; };  // type 0 grunt, 1 tough, 2 boss
  struct P { float x, y, vx, vy; int life; uint16_t c; };
  struct Cap { float x, y; int kind; };  // 0 triple shot, 1 shield, 2 life
  std::vector<B> bullets;
  std::vector<En> enemies;
  std::vector<P> parts;
  std::vector<Cap> caps;
  int score = 0, best = 0, lives = 3, wave = 0, power = 0, shield = 0;
  uint32_t lastShot = 0, hurtT = 0, waveT = 0;
  bool over = false;
  float stars[40][3];
  StarfallApp() { name = "STARFALL"; for (auto& s : stars) { s[0] = random(240); s[1] = random(12, 125); s[2] = random(1, 4); } }
  void reset() { score = 0; lives = 3; wave = 0; power = shield = 0; over = false; bullets.clear(); enemies.clear(); parts.clear(); caps.clear(); sx = 120; }
  void enter() override { best = loadScore("starfall"); if (over || !score) reset(); }
  void boom(float x, float y, uint16_t c, int n = 14) {
    for (int i = 0; i < n; i++) { float a = random(628) / 100.f, v = random(5, 25) / 10.f; parts.push_back({x, y, cosf(a) * v, sinf(a) * v, (int)random(10, 28), c}); }
  }
  void spawnWave() {
    wave++; waveT = millis();
    if (wave % 5 == 0) { enemies.push_back({120, -20, 0.8f, 30 + wave * 4, 2}); return; }
    int cols = 6, rows = std::min(3, 1 + wave / 2);
    for (int r = 0; r < rows; r++)
      for (int c = 0; c < cols; c++)
        enemies.push_back({(float)(28 + c * 36), (float)(-14 - r * 20), (wave % 2 ? 0.5f : -0.5f) * (1 + wave * 0.08f), 1 + (r == 0 && wave > 2) + wave / 6, r == 0 && wave > 2});
  }
  void die() { over = true; if (score > best) { best = score; saveScore("starfall", best); } }
  bool key(const KeyEv& e) override {
    if (over) { if (e.code == K_ENTER) { reset(); return true; } return false; }
    return gdir3(e) != K_NONE || (e.code == K_CHAR && e.ch == ' ');
  }
  void sprite(const char* const* rows, int h, int w, float x, float y, int sc, uint16_t c, uint16_t hi) {
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
      char ch = rows[j][i];
      if (ch == '.') continue;
      cv.fillRect((int)x + i * sc, (int)y + j * sc, sc, sc, ch == '+' ? hi : c);
    }
  }
  void tick() override {
    if (over) return;
    for (auto& s : stars) { s[1] += s[2] * 0.5f; if (s[1] > 125) { s[1] = 12; s[0] = random(240); } }
    if (held3(',') || held3('a')) sx -= 3.2f;
    if (held3('/') || held3('d')) sx += 3.2f;
    sx = constrain(sx, 10.f, 230.f);
    bool fire = held3(' ') || held3(';') || held3('w');
    if (fire && millis() - lastShot > (uint32_t)(power ? 140 : 230)) {
      lastShot = millis();
      bullets.push_back({sx, 100, 0, false});
      if (power) { bullets.push_back({sx - 6, 102, -0.6f, false}); bullets.push_back({sx + 6, 102, 0.6f, false}); }
      Audio::blip(1500, 15, Audio::W_SQUARE, 0.15f);
    }
    if (enemies.empty()) spawnWave();
    for (auto& en : enemies) {
      bool boss = en.type == 2;
      en.x += en.vx;
      en.y += en.y < (boss ? 26 : 16) ? 0.8f : (boss ? 0 : 0.05f + wave * 0.012f);
      float half = boss ? 21 : 7;
      if (en.x < half + 2 || en.x > 238 - half) { en.vx = -en.vx; if (!boss) en.y += 6; }
      int rate = boss ? 40 : 2 + wave;
      if (en.y > 14 && random(1000) < rate) {
        bullets.push_back({en.x, en.y + (boss ? 14 : 6), 0, true});
        if (boss && random(3) == 0) { bullets.push_back({en.x, en.y + 14, -0.8f, true}); bullets.push_back({en.x, en.y + 14, 0.8f, true}); }
      }
    }
    for (auto& b : bullets) { b.y += b.enemy ? 2.0f : -4.5f; b.x += b.vx; }
    for (size_t i = 0; i < bullets.size(); i++) {
      B& b = bullets[i];
      bool dead = b.y < 12 || b.y > 125 || b.x < 0 || b.x > 240;
      if (!b.enemy) {
        for (size_t j = 0; j < enemies.size() && !dead; j++) {
          En& en = enemies[j];
          float hw = en.type == 2 ? 21 : 8, hh = en.type == 2 ? 12 : 6;
          if (fabsf(en.x - b.x) < hw && fabsf(en.y - b.y) < hh) {
            dead = true;
            boom(b.x, b.y, T.yel, 3);
            if (--en.hp <= 0) {
              score += en.type == 2 ? 500 : en.type ? 50 : 10;
              boom(en.x, en.y, en.type == 2 ? T.red : en.type ? T.pink : T.cyan, en.type == 2 ? 60 : 16);
              if (en.type == 2 || random(8) == 0) caps.push_back({en.x, en.y, (int)random(3)});
              enemies.erase(enemies.begin() + j);
              Audio::blip(en.type == 2 ? 60 : 160, en.type == 2 ? 600 : 90, Audio::W_NOISE, 0.45f);
            }
          }
        }
      } else if (fabsf(b.x - sx) < 8 && b.y > 100 && b.y < 116 && millis() - hurtT > 1500) {
        dead = true;
        if (shield) { shield = 0; boom(sx, 108, T.cyan, 12); Audio::blip(900, 80, Audio::W_TRI); }
        else {
          lives--; hurtT = millis(); boom(sx, 108, T.red, 30);
          Audio::blip(90, 300, Audio::W_SAW, 0.5f);
          if (lives <= 0) die();
        }
      }
      if (dead) { bullets.erase(bullets.begin() + i); i--; }
    }
    for (size_t i = 0; i < caps.size(); i++) {
      caps[i].y += 0.9f;
      if (fabsf(caps[i].x - sx) < 10 && caps[i].y > 100 && caps[i].y < 118) {
        int k = caps[i].kind;
        if (k == 0) { power = 600; toast("TRIPLE SHOT!", 800); }
        else if (k == 1) { shield = 1; toast("SHIELD!", 800); }
        else { lives = std::min(5, lives + 1); toast("+1 LIFE", 800); }
        Audio::blip(1320, 120, Audio::W_SQUARE, 0.4f);
        caps.erase(caps.begin() + i); i--; continue;
      }
      if (caps[i].y > 125) { caps.erase(caps.begin() + i); i--; }
    }
    for (auto& en : enemies) if (en.y > 96 && en.type != 2) die();
    if (power) power--;
    for (size_t i = 0; i < parts.size(); i++) { auto& p = parts[i]; p.x += p.vx; p.y += p.vy; p.vy += 0.03f; if (--p.life <= 0) { parts.erase(parts.begin() + i); i--; } }
  }
  void draw() override {
    background(true);
    for (auto& s : stars) cv.drawPixel(s[0], s[1], s[2] > 2 ? T.grey : T.dim);
    static const char* shipA[6] = {"...+...", "..+#+..", ".#####.", "#######", "##.#.##", "#.....#"};
    static const char* gruntA[5] = {".#...#.", "..###..", ".#+#+#.", "#######", "#.#.#.#"};
    static const char* toughA[5] = {"#.###.#", "###+###", ".#####.", "##.#.##", ".#...#."};
    static const char* bossA[8] = {"...#######...", ".###########.", "##+##+#+##+##", "#############", "##.#######.##", "#...##.##...#", "....#...#....", "...#.....#..."};
    for (auto& en : enemies) {
      if (en.type == 2) {
        sprite(bossA, 8, 13, en.x - 19.5f, en.y - 12, 3, T.red, T.yel);
        int w = std::min(40, en.hp);
        cv.fillRect(100, 14, w * 2, 2, T.red);
      } else sprite(en.type ? toughA : gruntA, 5, 7, en.x - 7, en.y - 5, 2, en.type ? T.pink : gradc(fmodf(en.x / 240.f, 1)), T.white);
    }
    for (auto& c : caps) {
      uint16_t col = c.kind == 0 ? T.yel : c.kind == 1 ? T.cyan : T.green;
      cv.fillRoundRect(c.x - 5, c.y - 4, 10, 8, 2, col);
      char k[2] = {(char)(c.kind == 0 ? 'P' : c.kind == 1 ? 'S' : '+'), 0};
      text(c.x - 2, c.y - 3, k, T.bg0, 1, false);
    }
    for (auto& b : bullets) cv.fillRect(b.x - 1, b.y, 2, 5, b.enemy ? T.red : T.yel);
    for (auto& p : parts) cv.fillRect(p.x, p.y, 2, 2, lerpc(T.bg0, p.c, p.life / 28.f));
    bool blink = millis() - hurtT < 1500 && (millis() / 100) % 2;
    if (!blink && !over) {
      sprite(shipA, 6, 7, sx - 7, 104, 2, power ? T.yel : T.cyan, T.white);
      if (shield) cv.drawCircle(sx, 110, 11, T.cyan);
      if ((millis() / 60) % 2) cv.fillRect(sx - 2, 116, 4, 3, T.red);
    }
    statusBar("STARFALL", T.blue);
    char b[24];
    snprintf(b, sizeof(b), "%d  W%d", score, wave); text(80, 2, b, T.white, 1, false);
    for (int i = 0; i < lives; i++) cv.fillRect(150 + i * 6, 3, 4, 5, T.cyan);
    if (millis() - waveT < 1200 && !over) { snprintf(b, sizeof(b), wave % 5 == 0 ? "BOSS WAVE!" : "WAVE %d", wave); textC(120, 56, b, wave % 5 == 0 ? T.red : T.white, 2); }
    if (over) { snprintf(b, sizeof(b), "BEST %d - OK AGAIN", best); over3(120, "GAME OVER", b, T.red); }
    hintBar({{"<>", "MOVE"}, {"SPC", "FIRE"}, {"P S +", "POWERUPS"}});
  }
};

// ===================================================================== MINES
struct MinesApp : App {
  static constexpr int GW = 16, GH = 9, CS = 12, OX = 24, OY = 14;
  uint8_t mine[GH][GW], open_[GH][GW], flag[GH][GW];
  int cx = 8, cy = 4, mines = 20, best = 0;
  bool first = true, over = false, won = false;
  uint32_t t0 = 0, tEnd = 0;
  MinesApp() { name = "MINES"; }
  void reset() { memset(mine, 0, sizeof(mine)); memset(open_, 0, sizeof(open_)); memset(flag, 0, sizeof(flag)); first = true; over = won = false; }
  void enter() override { best = loadScore("mines"); if (over || won || first) reset(); }
  int around(int x, int y) {
    int n = 0;
    for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) { int a = x + i, b = y + j; if (a >= 0 && b >= 0 && a < GW && b < GH && mine[b][a]) n++; }
    return n;
  }
  void place(int sx, int sy) {
    int n = 0;
    while (n < mines) {
      int x = random(GW), y = random(GH);
      if (mine[y][x] || (abs(x - sx) <= 1 && abs(y - sy) <= 1)) continue;
      mine[y][x] = 1; n++;
    }
    t0 = millis();
  }
  void flood(int x, int y) {
    if (x < 0 || y < 0 || x >= GW || y >= GH || open_[y][x] || flag[y][x]) return;
    open_[y][x] = 1;
    if (!around(x, y)) for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) if (i || j) flood(x + i, y + j);
  }
  void reveal(int x, int y) {
    if (flag[y][x]) return;
    if (first) { place(x, y); first = false; }
    if (open_[y][x]) {  // chord: open neighbours if flags match
      int f = 0;
      for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) { int a = x + i, b = y + j; if (a >= 0 && b >= 0 && a < GW && b < GH && flag[b][a]) f++; }
      if (f == around(x, y))
        for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) { int a = x + i, b = y + j; if (a >= 0 && b >= 0 && a < GW && b < GH && !open_[b][a] && !flag[b][a]) { if (mine[b][a]) { boomAll(); return; } flood(a, b); } }
    } else {
      if (mine[y][x]) { boomAll(); return; }
      flood(x, y);
    }
    Audio::blip(900, 20, Audio::W_TRI, 0.3f);
    int closed = 0;
    for (int j = 0; j < GH; j++) for (int i = 0; i < GW; i++) if (!open_[j][i]) closed++;
    if (closed == mines) {
      won = true; tEnd = millis();
      int sec = (tEnd - t0) / 1000;
      if (!best || sec < best) { best = sec; saveScore("mines", best); }
      Audio::blip(1320, 200, Audio::W_SQUARE, 0.4f);
    }
  }
  void boomAll() { over = true; tEnd = millis(); for (int j = 0; j < GH; j++) for (int i = 0; i < GW; i++) if (mine[j][i]) open_[j][i] = 1; Audio::blip(70, 500, Audio::W_NOISE, 0.6f); }
  bool key(const KeyEv& e) override {
    if (over || won) { if (e.code == K_ENTER) { reset(); return true; } return false; }
    KeyCode k = gdir3(e);
    if (k == K_UP) { cy = (cy + GH - 1) % GH; return true; }
    if (k == K_DOWN) { cy = (cy + 1) % GH; return true; }
    if (k == K_LEFT) { cx = (cx + GW - 1) % GW; return true; }
    if (k == K_RIGHT) { cx = (cx + 1) % GW; return true; }
    if (e.code == K_ENTER || (e.code == K_CHAR && e.ch == ' ')) { reveal(cx, cy); return true; }
    if (e.is('f')) { if (!open_[cy][cx]) flag[cy][cx] ^= 1; return true; }
    if (e.is('m')) { mines = mines >= 35 ? 12 : mines + 8; reset(); char b[20]; snprintf(b, sizeof(b), "%d MINES", mines); toast(b); return true; }
    return false;
  }
  void draw() override {
    background(true);
    statusBar("MINES", T.blue);
    static const uint16_t numC[9] = {0, RGB(90, 160, 255), RGB(90, 240, 150), RGB(255, 90, 100), RGB(180, 110, 255), RGB(255, 160, 60), RGB(62, 232, 240), RGB(255, 255, 255), RGB(150, 150, 150)};
    int flags = 0;
    for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) {
      int px = OX + x * CS, py = OY + y * CS;
      if (flag[y][x]) flags++;
      if (open_[y][x]) {
        cv.fillRect(px, py, CS - 1, CS - 1, T.bg2);
        if (mine[y][x]) { cv.fillCircle(px + 5, py + 5, 3, T.red); cv.drawPixel(px + 4, py + 4, T.white); }
        else { int n = around(x, y); if (n) { char b[2] = {(char)('0' + n), 0}; text(px + 3, py + 2, b, numC[n], 1, false); } }
      } else {
        cv.fillRect(px, py, CS - 1, CS - 1, T.edge);
        cv.drawFastHLine(px, py, CS - 1, lerpc(T.edge, T.white, 0.3f));
        if (flag[y][x]) { cv.fillTriangle(px + 3, py + 2, px + 3, py + 7, px + 8, py + 4, T.yel); cv.drawFastVLine(px + 3, py + 2, 8, T.white); }
      }
      if (x == cx && y == cy && !over && !won) frame(px - 1, py - 1, CS + 1, CS + 1, T.cyan);
    }
    char b[24];
    uint32_t sec = first ? 0 : ((over || won) ? tEnd - t0 : millis() - t0) / 1000;
    snprintf(b, sizeof(b), "%d/%d  %us", flags, mines, (unsigned)sec); text(80, 2, b, T.white, 1, false);
    if (over) over3(120, "BOOM!", "OK = AGAIN", T.red);
    if (won) { snprintf(b, sizeof(b), "%us  BEST %ds", (unsigned)sec, best); over3(120, "CLEARED!", b, T.green); }
    hintBar({{"OK", "OPEN"}, {"F", "FLAG"}, {"M", "MINES"}, {"ESC", "BACK"}});
  }
};

// ===================================================================== DUEL
struct DuelApp : App {
  float py = 67, cy = 67, bx = 120, by = 67, vx = 2, vy = 1;
  int ps = 0, cs = 0, diff = 1;
  bool serve = true, over = false;
  DuelApp() { name = "DUEL"; }
  void reset() { ps = cs = 0; over = false; serveBall(1); }
  void serveBall(int dir) { bx = 120; by = 67; vx = 2.0f * dir; vy = random(-10, 11) / 10.f; serve = true; }
  void enter() override { reset(); }
  bool key(const KeyEv& e) override {
    if (over) { if (e.code == K_ENTER) { reset(); return true; } return false; }
    if (e.code == K_CHAR && e.ch == ' ') { serve = false; return true; }
    if (e.is('l')) { diff = diff % 3 + 1; toast(diff == 1 ? "CPU EASY" : diff == 2 ? "CPU MEDIUM" : "CPU HARD"); return true; }
    return gdir3(e) != K_NONE;
  }
  void tick() override {
    if (over) return;
    if (held3(';') || held3('w')) py -= 3;
    if (held3('.') || held3('s')) py += 3;
    py = constrain(py, 24.f, 112.f);
    float target = vx > 0 ? by : 67;
    float sp = 1.2f + diff * 0.6f;
    cy += constrain(target - cy, -sp, sp);
    cy = constrain(cy, 24.f, 112.f);
    if (serve) return;
    bx += vx; by += vy;
    if (by < 15 || by > 121) { vy = -vy; by = constrain(by, 15.f, 121.f); Audio::blip(300, 15, Audio::W_SQUARE, 0.2f); }
    if (bx < 12 && bx > 6 && fabsf(by - py) < 12) { vx = fabsf(vx) * 1.05f; vy = (by - py) / 5; Audio::blip(600, 20, Audio::W_SQUARE, 0.3f); }
    if (bx > 228 && bx < 234 && fabsf(by - cy) < 12) { vx = -fabsf(vx) * 1.05f; vy = (by - cy) / 5; Audio::blip(500, 20, Audio::W_SQUARE, 0.3f); }
    vx = constrain(vx, -6.f, 6.f);
    if (bx < 0) { cs++; Audio::blip(150, 200, Audio::W_SAW); serveBall(-1); }
    if (bx > 240) { ps++; Audio::blip(1000, 120, Audio::W_TRI); serveBall(1); }
    if (ps >= 7 || cs >= 7) over = true;
  }
  void draw() override {
    background();
    for (int y = 14; y < 125; y += 6) cv.fillRect(119, y, 2, 3, T.dim);
    cv.fillRect(6, py - 10, 4, 20, T.cyan);
    cv.fillRect(230, cy - 10, 4, 20, T.pink);
    cv.fillRect(bx - 2, by - 2, 4, 4, T.white);
    char b[8];
    snprintf(b, sizeof(b), "%d", ps); textC(90, 18, b, T.cyan, 3);
    snprintf(b, sizeof(b), "%d", cs); textC(150, 18, b, T.pink, 3);
    statusBar("DUEL", T.blue);
    if (serve && !over) textC(120, 60, "SPACE TO SERVE", T.grey);
    if (over) over3(120, ps > cs ? "YOU WIN!" : "CPU WINS", "OK = AGAIN", ps > cs ? T.green : T.red);
    hintBar({{"^v", "MOVE"}, {"SPC", "SERVE"}, {"L", "LEVEL"}, {"FIRST TO", "7"}});
  }
};

// ===================================================================== ECHO (memory)
struct EchoApp : App {
  std::vector<uint8_t> seq;
  int pos = 0, best = 0, showIdx = -1;
  bool showing = false, over = false;
  uint32_t t = 0, lit = 0;
  int litPad = -1;
  const uint8_t notes[4] = {64, 67, 71, 74};
  EchoApp() { name = "ECHO"; }
  void flash(int p) { litPad = p; lit = millis(); Audio::blip(440.f * powf(2.f, (notes[p] - 69) / 12.f), 260, Audio::W_TRI, 0.5f); }
  void next() { seq.push_back(random(4)); showing = true; showIdx = -1; t = millis() + 600; pos = 0; }
  void enter() override { best = loadScore("echo"); seq.clear(); over = false; next(); }
  bool key(const KeyEv& e) override {
    if (over) { if (e.code == K_ENTER) { enter(); return true; } return false; }
    if (showing) return e.code != K_ESC;
    int p = -1;
    KeyCode k = gdir3(e);
    if (k == K_UP) p = 0; else if (k == K_RIGHT) p = 1; else if (k == K_DOWN) p = 2; else if (k == K_LEFT) p = 3;
    if (e.code == K_CHAR && e.ch >= '1' && e.ch <= '4') p = e.ch - '1';
    if (p < 0) return false;
    flash(p);
    if (p != seq[pos]) {
      over = true; Audio::blip(110, 500, Audio::W_SAW, 0.5f);
      int sc = seq.size() - 1;
      if (sc > best) { best = sc; saveScore("echo", best); }
      return true;
    }
    if (++pos >= (int)seq.size()) { t = millis() + 700; showing = true; seq.push_back(random(4)); showIdx = -1; pos = 0; }
    return true;
  }
  void tick() override {
    if (!showing || over || millis() < t) return;
    showIdx++;
    if (showIdx >= (int)seq.size()) { showing = false; return; }
    flash(seq[showIdx]);
    t = millis() + std::max(250, 600 - (int)seq.size() * 20);
  }
  void draw() override {
    background();
    statusBar("ECHO", T.blue);
    uint16_t cols[4] = {T.cyan, T.green, T.pink, T.yel};
    int px[4] = {120, 160, 120, 80}, py[4] = {34, 68, 102, 68};
    for (int i = 0; i < 4; i++) {
      bool on = i == litPad && millis() - lit < 250;
      uint16_t c = on ? lerpc(cols[i], T.white, 0.4f) : lerpc(T.bg1, cols[i], 0.35f);
      cv.fillRoundRect(px[i] - 16, py[i] - 14, 32, 28, 6, c);
      if (on) cv.drawRoundRect(px[i] - 18, py[i] - 16, 36, 32, 7, cols[i]);
    }
    char b[24];
    snprintf(b, sizeof(b), "LEVEL %d", (int)seq.size()); text(6, 18, b, T.white);
    snprintf(b, sizeof(b), "BEST %d", best); text(6, 30, b, T.yel, 1, false);
    text(180, 18, showing ? "LISTEN" : "REPEAT!", showing ? T.grey : T.green);
    if (over) { snprintf(b, sizeof(b), "SCORE %d - OK AGAIN", (int)seq.size() - 1); over3(120, "WRONG!", b, T.red); }
    hintBar({{"ARROWS", "PADS"}, {"1-4", "PADS"}, {"ESC", "BACK"}});
  }
};

// ===================================================================== RUNNER (coins, double jump, power-ups, day/night)
struct RunnerApp : App {
  float y = 0, vy = 0, speed = 2.8f, dist = 0;
  int jumps = 0, coins = 0, best = 0, combo = 0, shield = 0, magnet = 0;
  bool duck = false, over = false, started = false;
  struct Ob { float x; int type; };   // 0 crate, 1 tall pillar, 2 low flyer, 3 high flyer, 4 double crate
  struct Coin { float x, y; bool got; };
  struct Pow { float x, y; int kind; };  // 0 shield, 1 magnet
  struct Dust { float x, y, v; int life; };
  std::vector<Ob> obs;
  std::vector<Coin> coinsV;
  std::vector<Pow> pows;
  std::vector<Dust> dust;
  float nextOb = 140, nextCoin = 80;
  uint32_t flashT = 0;
  const int GROUND = 108, PX = 34;
  RunnerApp() { name = "RUNNER"; }
  int score() { return (int)(dist / 10) + coins * 10; }
  void reset() { y = vy = 0; speed = 2.8f; dist = 0; jumps = 0; coins = combo = shield = magnet = 0; obs.clear(); coinsV.clear(); pows.clear(); dust.clear(); over = started = false; nextOb = 140; nextCoin = 80; }
  void enter() override { best = loadScore("runner"); reset(); }
  bool key(const KeyEv& e) override {
    bool jump = (e.code == K_CHAR && e.ch == ' ') || gdir3(e) == K_UP || e.code == K_ENTER;
    if (over) { if (jump) { reset(); return true; } return false; }
    if (jump && jumps < 2) {
      vy = jumps ? 4.0f : 4.8f; jumps++; started = true;
      Audio::blip(jumps == 1 ? 660 : 990, 40, Audio::W_SQUARE, 0.25f);
      for (int i = 0; i < 5; i++) dust.push_back({(float)PX + 4, (float)(GROUND - y), -(float)random(5, 15) / 10, 12});
      return true;
    }
    return gdir3(e) == K_DOWN || jump;
  }
  void tick() override {
    if (!started || over) return;
    duck = held3('.') || held3('s');
    vy -= duck ? 0.7f : 0.3f;
    y += vy;
    if (y <= 0) { y = 0; vy = 0; jumps = 0; }
    speed = std::min(7.5f, 2.8f + dist / 1800);
    dist += speed;
    nextOb -= speed; nextCoin -= speed;
    if (nextOb <= 0) {
      int r = random(100), ty = r < 35 ? 0 : r < 55 ? 1 : r < 70 ? 4 : (dist > 600 ? (r < 85 ? 2 : 3) : 0);
      obs.push_back({250, ty});
      if (random(10) == 0 && dist > 400) pows.push_back({330, 60, (int)random(2)});
      nextOb = random(110, 190) + speed * 10;
    }
    if (nextCoin <= 0) {  // coin arcs
      int n = random(3, 7); bool arc = random(2);
      for (int i = 0; i < n; i++) coinsV.push_back({(float)(250 + i * 14), (float)(arc ? 14 + 22 * sinf(i * PI / (n - 1)) : 12), false});
      nextCoin = random(160, 300);
    }
    float px = PX, pw = 12, ph = duck && y <= 0 ? 10 : 20, pyb = y;
    for (size_t i = 0; i < obs.size(); i++) {
      Ob& o = obs[i];
      o.x -= speed;
      if (o.x < -30) { obs.erase(obs.begin() + i); i--; combo = std::min(combo + 1, 20); continue; }
      float ow = o.type == 1 ? 10 : o.type == 4 ? 26 : 14, oh = o.type == 1 ? 24 : 13, oyb = o.type == 2 ? 14 : o.type == 3 ? 26 : 0;
      if (o.type >= 2 && o.type <= 3) { ow = 16; oh = 8; }
      bool hit = px + pw > o.x && px < o.x + ow && pyb < oyb + oh && pyb + ph > oyb;
      if (hit) {
        if (shield) { shield = 0; obs.erase(obs.begin() + i); i--; flashT = millis(); Audio::blip(1200, 100, Audio::W_TRI); continue; }
        over = true; Audio::blip(100, 300, Audio::W_NOISE, 0.5f);
        if (score() > best) { best = score(); saveScore("runner", best); }
        return;
      }
    }
    for (auto& c : coinsV) {
      c.x -= speed;
      if (magnet) { float dx = PX + 6 - c.x, dyy = (y + 10) - c.y; if (fabsf(dx) < 60) { c.x += dx * 0.15f; c.y += dyy * 0.15f; } }
      if (!c.got && fabsf(c.x - (PX + 6)) < 10 && fabsf(c.y - (y + 10)) < 14) { c.got = true; coins++; Audio::blip(1600 + (coins % 5) * 120, 40, Audio::W_SQUARE, 0.2f); }
    }
    coinsV.erase(std::remove_if(coinsV.begin(), coinsV.end(), [](const Coin& c) { return c.got || c.x < -10; }), coinsV.end());
    for (size_t i = 0; i < pows.size(); i++) {
      pows[i].x -= speed;
      if (fabsf(pows[i].x - (PX + 6)) < 12 && fabsf(pows[i].y - (y + 10)) < 16) {
        if (pows[i].kind == 0) { shield = 1; toast("SHIELD!", 700); } else { magnet = 600; toast("COIN MAGNET!", 700); }
        Audio::blip(1320, 150, Audio::W_SQUARE, 0.4f);
        pows.erase(pows.begin() + i); i--; continue;
      }
      if (pows[i].x < -10) { pows.erase(pows.begin() + i); i--; }
    }
    if (magnet) magnet--;
    for (size_t i = 0; i < dust.size(); i++) { dust[i].x += dust[i].v - speed * 0.3f; if (--dust[i].life <= 0) { dust.erase(dust.begin() + i); i--; } }
    if ((int)(dist / 10) % 250 == 0 && dist > 50) { Audio::blip(880, 60, Audio::W_TRI, 0.2f); }
  }
  void draw() override {
    // day -> sunset -> night cycle
    float cyc = fmodf(dist / 4000.f, 1.f);
    uint16_t sky = cyc < 0.4f ? RGB(20, 36, 70) : cyc < 0.55f ? RGB(70, 30, 60) : T.bg0;
    cv.fillSprite(sky);
    if (cyc >= 0.5f) for (int i = 0; i < 30; i++) cv.drawPixel((i * 53 + (int)(dist * 0.05f)) % 240, 14 + (i * 37) % 60, T.grey);
    else cv.fillCircle(200, 30, 9, cyc < 0.4f ? T.yel : RGB(255, 140, 80));
    for (int x = 0; x < 240; x++) {  // far hills + near hills
      int h1 = GROUND - 30 + (int)(10 * sinf((x + dist * 0.1f) * 0.02f));
      cv.drawFastVLine(x, h1, GROUND - h1, lerpc(sky, T.blueD, 0.7f));
      int h2 = GROUND - 14 + (int)(6 * sinf((x + dist * 0.3f) * 0.045f));
      cv.drawFastVLine(x, h2, GROUND - h2, lerpc(sky, T.purpD, 0.8f));
    }
    cv.fillRect(0, GROUND, 240, 125 - GROUND, T.bg1);
    cv.drawFastHLine(0, GROUND, 240, T.green);
    for (int x = -((int)dist % 16); x < 240; x += 16) cv.fillRect(x, GROUND + 4, 6, 1, T.dim);
    for (auto& c : coinsV) {
      int cy = GROUND - (int)c.y - 4;
      int w = 2 + (int)(3 * fabsf(sinf(millis() / 150.f + c.x)));
      cv.fillEllipse(c.x, cy, w, 4, T.yel);
      cv.drawFastVLine(c.x, cy - 2, 4, lerpc(T.yel, T.white, 0.5f));
    }
    for (auto& p : pows) {
      int py = GROUND - (int)p.y;
      cv.fillRoundRect(p.x - 6, py - 6, 12, 12, 3, p.kind ? T.pink : T.cyan);
      text(p.x - 2, py - 3, p.kind ? "M" : "S", T.bg0, 1, false);
    }
    for (auto& o : obs) {
      if (o.type == 2 || o.type == 3) {
        int fy = GROUND - (o.type == 2 ? 14 : 26) - 8;
        bool flap = ((int)(dist / 8)) % 2;
        cv.fillRect(o.x + 2, fy + 3, 12, 4, T.red);
        cv.fillTriangle(o.x + 4, fy + 4, o.x + 11, fy + 4, o.x + 7, flap ? fy - 3 : fy + 10, lerpc(T.red, T.white, 0.3f));
        cv.drawPixel(o.x + 3, fy + 4, T.white);
      } else {
        int h = o.type == 1 ? 24 : 13, w = o.type == 1 ? 10 : o.type == 4 ? 26 : 14;
        cv.fillRect(o.x, GROUND - h, w, h, T.green);
        frame(o.x, GROUND - h, w, h, lerpc(T.green, T.bg0, 0.4f));
        for (int k = 3; k < h - 2; k += 5) cv.drawFastHLine(o.x + 2, GROUND - h + k, w - 4, lerpc(T.green, T.bg0, 0.3f));
      }
    }
    for (auto& d : dust) cv.fillRect(d.x, GROUND - 2 - (12 - d.life) / 3, 2, 2, lerpc(T.bg1, T.grey, d.life / 12.f));
    // player (2x pixel art)
    int py = GROUND - (int)y;
    static const char* runA[10] = {"..####..", "..#+##..", "..####..", "...##...", ".######.", "#.####.#", "..####..", "..#..#..", ".#....#.", "#......#"};
    static const char* runB[10] = {"..####..", "..#+##..", "..####..", "...##...", ".######.", "#.####.#", "..####..", "..#..#..", "..#..#..", "..#..#.."};
    static const char* duckA[5] = {".....###", "######+#", "########", ".######.", ".#....#."};
    uint16_t pc = millis() - flashT < 300 ? T.white : T.cyan;
    if (duck && y <= 0) { for (int j = 0; j < 5; j++) for (int i = 0; i < 8; i++) if (duckA[j][i] != '.') cv.fillRect(PX - 2 + i * 2, py - 10 + j * 2, 2, 2, duckA[j][i] == '+' ? T.white : pc); }
    else {
      const char** a = (y > 0 || (int)(dist / 14) % 2) ? runA : runB;
      for (int j = 0; j < 10; j++) for (int i = 0; i < 8; i++) if (a[j][i] != '.') cv.fillRect(PX - 2 + i * 2, py - 20 + j * 2, 2, 2, a[j][i] == '+' ? T.white : pc);
    }
    if (shield) cv.drawCircle(PX + 6, py - 10, 14, T.cyan);
    if (magnet) cv.drawCircle(PX + 6, py - 10, 16 + (millis() / 80) % 4, T.pink);
    statusBar("RUNNER", T.blue);
    char b[32];
    snprintf(b, sizeof(b), "%05d  $%d  HI %d", score(), coins, best); text(64, 2, b, T.white, 1, false);
    if (!started) { textC(120, 40, "SPACE: JUMP (TWICE = DOUBLE)", T.white); textC(120, 52, "v: DUCK   COLLECT COINS!", T.grey); }
    if (over) { snprintf(b, sizeof(b), "%d PTS - SPACE AGAIN", score()); over3(120, "OOPS!", b, T.red); }
    hintBar({{"SPC", "JUMP x2"}, {"v", "DUCK"}, {"S M", "POWERUPS"}});
  }
};

App* appStarfall = new StarfallApp();
App* appMines = new MinesApp();
App* appDuel = new DuelApp();
App* appEcho = new EchoApp();
App* appRunner = new RunnerApp();
