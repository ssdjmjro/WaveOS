// WAVE OS - games: Snake + 2048 (high scores on SD)
#include "common.h"
#include "audio.h"

// ---- background SD writes: the card can take seconds per file, so whole files are queued here and
// written by a short-lived low priority task (a newer write of the same file replaces a queued one)
struct SdJob { String path; String* data; };
static std::vector<SdJob> sdJobs;
static SemaphoreHandle_t sdJobMx = nullptr;
static volatile bool sdJobRun = false;
static void sdJobTask(void*) {
  for (;;) {
    xSemaphoreTake(sdJobMx, portMAX_DELAY);
    if (sdJobs.empty()) { sdJobRun = false; xSemaphoreGive(sdJobMx); break; }
    SdJob j = sdJobs.front(); sdJobs.erase(sdJobs.begin());
    xSemaphoreGive(sdJobMx);
    uint32_t t0 = millis();
    File f = SD.open(j.path, FILE_WRITE);
    uint32_t t1 = millis();
    if (f) {  // in small pieces so the music decoder can read the card in between
      const uint8_t* p = (const uint8_t*)j.data->c_str();
      size_t n = j.data->length();
      for (size_t o = 0; o < n; o += 512) { f.write(p + o, std::min<size_t>(512, n - o)); vTaskDelay(1); }
      f.close();
    }
    Serial.printf("[perf] wrote %s (%u B) open=%ums total=%ums (background)\n", j.path.c_str(), (unsigned)j.data->length(), (unsigned)(t1 - t0), (unsigned)(millis() - t0));
    delete j.data;
  }
  vTaskDelete(nullptr);
}
void sdWriteAsync(const char* path, String* data) {
  if (!sdOK) { delete data; return; }
  if (!sdJobMx) sdJobMx = xSemaphoreCreateMutex();
  xSemaphoreTake(sdJobMx, portMAX_DELAY);
  bool replaced = false;
  for (auto& j : sdJobs) if (j.path == path) { delete j.data; j.data = data; replaced = true; }
  if (!replaced) sdJobs.push_back({String(path), data});
  bool start = !sdJobRun;
  if (start) sdJobRun = true;
  xSemaphoreGive(sdJobMx);
  if (start && xTaskCreatePinnedToCore(sdJobTask, "sdwrite", 4096, nullptr, 1, nullptr, 0) != pdPASS) sdJobTask(nullptr);
}
bool sdWriteBusy() { return sdJobRun; }

// ---- saved app state: "WSAV <bytes> <checksum>\n" + hex. Written in the background like scores.
static uint32_t stateSum(const uint8_t* d, size_t n) { uint32_t h = 2166136261u; for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 16777619u; return h; }
void stateWrite(const char* key, const std::vector<uint8_t>& d) {
  if (!sdOK || !key) return;
  static bool dirMade = false;
  if (!dirMade) { ensureDir("/waveos/save"); dirMade = true; }
  String* s = new String();
  s->reserve(d.size() * 2 + 40);
  char h[40];
  snprintf(h, sizeof(h), "WSAV %u %08x\n", (unsigned)d.size(), (unsigned)stateSum(d.data(), d.size()));
  *s += h;
  static const char* hx = "0123456789abcdef";
  for (size_t i = 0; i < d.size(); i++) { *s += hx[d[i] >> 4]; *s += hx[d[i] & 15]; if (i % 48 == 47) *s += '\n'; }
  *s += '\n';
  char p[48]; snprintf(p, sizeof(p), "/waveos/save/%s.sav", key);
  sdWriteAsync(p, s);
}
bool stateRead(const char* key, std::vector<uint8_t>& out) {
  out.clear();
  if (!sdOK || !key) return false;
  char p[48]; snprintf(p, sizeof(p), "/waveos/save/%s.sav", key);
  File f = SD.open(p);
  if (!f) return false;
  String hd = f.readStringUntil('\n');
  unsigned n = 0, sum = 0;
  if (sscanf(hd.c_str(), "WSAV %u %x", &n, &sum) != 2 || n > 8192) { f.close(); return false; }
  out.reserve(n);
  int hi = -1;
  while (f.available() && out.size() < n) {
    int c = f.read(), v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (v < 0) continue;
    if (hi < 0) hi = v; else { out.push_back((uint8_t)(hi << 4 | v)); hi = -1; }
  }
  f.close();
  if (out.size() != n || stateSum(out.data(), n) != sum) { out.clear(); return false; }  // cut off / damaged
  return true;
}
void appStateSave(App* a) {
  if (!a || !a->saveKey()) return;
  Persist p;
  a->persist(p);
  stateWrite(a->saveKey(), p.buf);
  a->stateDirty = false;
}
void appStateLoad(App* a) {
  if (!a || a->stateTried || !a->saveKey()) return;
  a->stateTried = true;
  Persist probe;
  a->persist(probe);                 // how many bytes this firmware's layout needs
  Persist p; p.loading = true;
  if (!stateRead(a->saveKey(), p.buf) || p.buf.size() != probe.buf.size()) return;
  a->persist(p);
  Serial.printf("[state] %s restored (%u B)\n", a->saveKey(), (unsigned)p.buf.size());
}

// ---- new best: a short fanfare + toast, played from loop() so it doesn't clash with the game over sound
static uint32_t fanfareT = 0;
void scoreTick() {
  if (!fanfareT) return;
  static const uint16_t notes[4] = {784, 988, 1175, 1568};
  uint32_t el = millis() - fanfareT;
  static int done = 0;
  if (el < 250) { done = 0; return; }
  while (done < 4 && el >= 250 + done * 110u) { Audio::blip(notes[done], done == 3 ? 220 : 90, Audio::W_SQUARE, 0.35f); done++; }
  if (done >= 4) fanfareT = 0;
}

// all scores live in RAM after the first read; saving rewrites the file in the background
static std::vector<std::pair<String, int>> scores;
static bool scoresLoaded = false;
static void loadScores() {
  if (scoresLoaded) return;
  scoresLoaded = true;
  File f = SD.open("/waveos/scores.txt");
  if (!f) return;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    int e = l.indexOf('=');
    if (e > 0) scores.push_back({l.substring(0, e), (int)l.substring(e + 1).toInt()});
  }
  f.close();
}
int loadScore(const char* key) {
  loadScores();
  for (auto& c : scores) if (c.first == key) return c.second;
  return 0;
}
void saveScore(const char* key, int val, bool isBest) {
  if (!sdOK) return;
  loadScores();
  bool found = false;
  for (auto& c : scores) if (c.first == key) { c.second = val; found = true; }
  if (!found) scores.push_back({String(key), val});
  String* out = new String();
  for (auto& c : scores) { *out += c.first; *out += '='; *out += String(c.second); *out += "\r\n"; }
  sdWriteAsync("/waveos/scores.txt", out);
  if (isBest && val > 0) { toast("NEW BEST!", 1600); fanfareT = millis(); }
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
    if (!started && !over) textC(GX + CW * CS / 2, 30, "ARROWS TO START", T.white);
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
  int score = 0, uscore = 0, best = 0, saved = 0;
  bool over = false, won = false, keepGoing = false, canUndo = false;
  // animation: tiles slide from where they were (90ms), merged tiles pop, the new tile grows in
  struct Mv { int8_t fx, fy, tx, ty; int v; };
  Mv mv[16]; int nmv = 0;
  bool merged[4][4];
  uint32_t animT = 0;
  int popX = -1, popY = -1, gain = 0;
  static constexpr uint32_t SLIDE_MS = 90;
  G2048App() { name = "MERGE"; }
  const char* saveKey() override { return "merge"; }
  void persist(Persist& p) override {
    p.io(g); p.io(score); p.io(over); p.io(won); p.io(keepGoing);
    if (p.loading) { canUndo = false; nmv = 0; animT = 0; popX = popY = -1; memset(merged, 0, sizeof(merged)); }
  }
  void enter() override { best = saved = loadScore("2048"); if (!score && !over) reset(); }
  void leave() override { saveBest(); }
  void saveBest() { if (best > saved) { saved = best; saveScore("2048", best); } }
  void reset() {
    memset(g, 0, sizeof(g)); memset(merged, 0, sizeof(merged));
    score = 0; over = false; won = false; keepGoing = false; canUndo = false; nmv = 0; gain = 0;
    spawn(); spawn();
  }
  void spawn() {
    int empty = 0;
    for (auto& r : g) for (int v : r) if (!v) empty++;
    if (!empty) return;
    int k = random(empty);
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) if (!g[y][x] && k-- == 0) { g[y][x] = random(10) ? 2 : 4; popX = x; popY = y; }
  }
  bool slide(int dx, int dy) {
    bool moved = false;
    nmv = 0; memset(merged, 0, sizeof(merged));
    for (int line = 0; line < 4; line++) {
      int vals[4], src[4], n = 0;
      auto at = [&](int i, int& x, int& y) { int idx = (dx > 0 || dy > 0) ? 3 - i : i; x = dx ? idx : line; y = dx ? line : idx; };
      for (int i = 0; i < 4; i++) { int x, y; at(i, x, y); if (g[y][x]) { vals[n] = g[y][x]; src[n++] = i; } }
      int out[4] = {0, 0, 0, 0}, m = 0;
      for (int i = 0; i < n; i++) {
        int tx, ty; at(m, tx, ty);
        int fx, fy;
        at(src[i], fx, fy); mv[nmv++] = {(int8_t)fx, (int8_t)fy, (int8_t)tx, (int8_t)ty, vals[i]};
        if (i + 1 < n && vals[i] == vals[i + 1]) {
          at(src[i + 1], fx, fy); mv[nmv++] = {(int8_t)fx, (int8_t)fy, (int8_t)tx, (int8_t)ty, vals[i]};
          out[m] = vals[i] * 2; score += vals[i] * 2; merged[ty][tx] = true;
          if (vals[i] * 2 == 2048) won = true;
          i++;
        } else out[m] = vals[i];
        m++;
      }
      for (int i = 0; i < 4; i++) {
        int x, y; at(i, x, y);
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
    if (e.is('u') && canUndo) { memcpy(g, ug, sizeof(g)); score = uscore; over = false; canUndo = false; nmv = 0; popX = -1; return true; }
    if (e.is('n')) { saveBest(); reset(); return true; }
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
      animT = millis(); gain = score - ts;
      Audio::blip(score > ts ? 520 + std::min(8, (int)log2f(std::max(1, gain))) * 60 : 330, 30, Audio::W_TRI);
      if (score > best) best = score;
      if (!anyMove()) { over = true; saveBest(); Audio::blip(150, 300, Audio::W_SAW); }
    } else nmv = 0;
    return true;
  }
  uint16_t tileCol(int v) {
    int l = 0; while (v > 1) { v >>= 1; l++; }
    if (l <= 0) return T.bg2;
    if (cfg.mono) return lerpc(T.dim, T.white, std::min(l, 11) / 11.f);
    static const uint16_t pal[11] = {0x6B6D, 0x8C51, 0x2D9F, 0x1C9F, 0x5A3F, 0x981F, 0xF80F, 0xFA20, 0xFC40, 0xFEA0, 0xFFE0};
    return pal[std::min(l, 11) - 1];
  }
  void tile(int px, int py, int ts, int v, int inset) {
    uint16_t c = tileCol(v);
    int w = ts - inset * 2;
    if (w <= 2) return;
    cv.fillRoundRect(px + inset, py + inset, w, w, 3, c);
    cv.drawFastHLine(px + inset + 2, py + inset, w - 4, lerpc(c, T.white, 0.45f));
    cv.drawFastHLine(px + inset + 2, py + inset + w - 1, w - 4, lerpc(c, T.bg0, 0.45f));
    if (w < ts - 6) return;
    char b[8]; snprintf(b, sizeof(b), "%d", v);
    int sc = v < 100 ? 2 : 1;
    textC(px + ts / 2, py + ts / 2 - (sc == 2 ? 7 : 3), b, T.white, sc, true);
  }
  void draw() override {
    background();
    statusBar("MERGE", T.blue);
    int ts = 26, gx = 8, gy = 16;
    panel(gx - 3, gy - 3, 4 * (ts + 2) + 4, 4 * (ts + 2) + 4, T.blueD, T.bg0);
    uint32_t el = millis() - animT;
    bool sliding = nmv && el < SLIDE_MS;
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) cv.fillRoundRect(gx + x * (ts + 2), gy + y * (ts + 2), ts, ts, 3, T.bar);
    if (sliding) {
      float t = el / (float)SLIDE_MS; t = 1 - (1 - t) * (1 - t);  // ease out
      for (int i = 0; i < nmv; i++) {
        float fx = mv[i].fx + (mv[i].tx - mv[i].fx) * t, fy = mv[i].fy + (mv[i].ty - mv[i].fy) * t;
        tile(gx + (int)lroundf(fx * (ts + 2)), gy + (int)lroundf(fy * (ts + 2)), ts, mv[i].v, 0);
      }
    } else {
      uint32_t ae = el - SLIDE_MS;  // time since the slide ended
      for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
        int v = g[y][x];
        if (!v) continue;
        int inset = 0;
        if (merged[y][x] && ae < 120) inset = -(int)(2 * sinf(PI * ae / 120.f));      // pop
        if (x == popX && y == popY && ae < 120) inset = (int)(ts / 2 * (1 - ae / 120.f));  // grow in
        tile(gx + x * (ts + 2), gy + y * (ts + 2), ts, v, inset);
      }
    }
    int sx = 128;
    char b[16];
    panel(sx, 15, 108, 34, T.blueD);
    text(sx + 6, 19, "SCORE", T.grey);
    snprintf(b, sizeof(b), "%d", score);
    text(sx + 6, 30, b, T.white, 2);
    if (gain > 0 && el < 600) {  // +points float up
      snprintf(b, sizeof(b), "+%d", gain);
      text(sx + 102 - textW(b), 30 - (int)(el / 60), b, lerpc(T.yel, T.panel, el / 600.f), 1, false);
    }
    panel(sx, 52, 108, 22, T.blueD);
    text(sx + 6, 56, "BEST", T.grey);
    snprintf(b, sizeof(b), "%d", best);
    text(sx + 40, 56, b, score >= best && score ? T.green : T.yel);
    panel(sx, 77, 108, 46, T.blueD);
    int big = 0; for (auto& r : g) for (int v : r) big = std::max(big, v);
    snprintf(b, sizeof(b), "TOP TILE %d", big);
    text(sx + 6, 81, b, tileCol(big), 1, false);
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
