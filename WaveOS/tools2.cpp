// WAVE OS - more tools: LEVEL (spirit level with the IMU) and CONVERT (unit converter)
#include "common.h"
#include "audio.h"

// ===================================================================== LEVEL
struct LevelApp : App {
  float fx = 0, fy = 0, fz = 1;        // filtered acceleration (g)
  float ox = 0, oy = 0;                // calibration offset (degrees)
  bool have = false, beep = true, wasLevel = false;
  uint32_t beepT = 0;
  LevelApp() { name = "LEVEL"; }
  void enter() override { have = false; }
  bool key(const KeyEv& e) override {
    if (e.code == K_ENTER) {  // calibrate: the current position is "level"
      float rx, ry; angles(rx, ry);
      ox += rx; oy += ry;
      toast("CALIBRATED", 800); return true;
    }
    if (e.code == K_BACK) { ox = oy = 0; toast("CALIBRATION CLEARED", 800); return true; }
    if (e.is('s')) { beep = !beep; toast(beep ? "BEEP ON" : "BEEP OFF", 700); return true; }
    return false;
  }
  void angles(float& rx, float& ry) {  // degrees, after calibration
    float s = cfg.rot == 3 ? -1 : 1;
    rx = atan2f(fx * s, fabsf(fz) + 1e-3f) * 180 / PI - ox;
    ry = atan2f(fy * s, fabsf(fz) + 1e-3f) * 180 / PI - oy;
  }
  void tick() override {
    float ax, ay, az;
    if (!imuAccel(ax, ay, az)) return;
    if (!have) { fx = ax; fy = ay; fz = az; have = true; }
    fx += (ax - fx) * 0.2f; fy += (ay - fy) * 0.2f; fz += (az - fz) * 0.2f;
  }
  void draw() override {
    background();
    statusBar("LEVEL", T.yel);
    if (!have) { textC(120, 60, "NO MOTION SENSOR", T.red); hintBar({{"ESC", "BACK"}}); return; }
    bool edge = fabsf(fz) < 0.55f;  // standing on an edge: one axis, like a classic level
    char b[32];
    if (!edge) {
      float rx, ry; angles(rx, ry);
      bool lvl = fabsf(rx) < 0.6f && fabsf(ry) < 0.6f;
      // round vial
      int cx = 62, cy = 68, R = 46;
      uint16_t ring = lvl ? T.green : T.edge;
      cv.fillCircle(cx, cy, R, lerpc(T.bg0, lvl ? T.green : T.yel, 0.12f));
      cv.drawCircle(cx, cy, R, ring); cv.drawCircle(cx, cy, R - 1, ring);
      cv.drawCircle(cx, cy, 9, T.grey);
      cv.drawFastHLine(cx - R + 4, cy, R * 2 - 8, T.dim); cv.drawFastVLine(cx, cy - R + 4, R * 2 - 8, T.dim);
      float bx = -rx / 20.f, by = -ry / 20.f, m = sqrtf(bx * bx + by * by);  // 20 degrees = edge of the vial
      if (m > 1) { bx /= m; by /= m; }
      int px = cx + (int)(bx * (R - 9)), py = cy + (int)(by * (R - 9));
      cv.fillSmoothCircle(px, py, 7, lvl ? T.green : T.yel);
      cv.fillCircle(px - 2, py - 2, 2, T.white);
      // readout
      text(124, 22, "X", T.grey); snprintf(b, sizeof(b), "%+.1f", rx); text(140, 18, b, fabsf(rx) < 0.6f ? T.green : T.white, 2);
      text(124, 46, "Y", T.grey); snprintf(b, sizeof(b), "%+.1f", ry); text(140, 42, b, fabsf(ry) < 0.6f ? T.green : T.white, 2);
      text(212, 18, "o", T.grey); text(212, 42, "o", T.grey);
      textC(178, 72, lvl ? "LEVEL" : "TILTED", lvl ? T.green : T.dim, 2);
      if (beep && lvl && !wasLevel) Audio::blip(1500, 50, Audio::W_SINE, 0.3f);
      wasLevel = lvl;
    } else {
      // tube vial: angle of the bottom edge against true horizontal / vertical
      float a = atan2f(fx, fy) * 180 / PI;
      float n = roundf(a / 90) * 90, d = a - n;
      bool lvl = fabsf(d) < 0.5f;
      panel(20, 40, 200, 26, lvl ? T.green : T.edge, lerpc(T.bg0, lvl ? T.green : T.yel, 0.12f));
      cv.drawFastVLine(108, 42, 22, T.grey); cv.drawFastVLine(132, 42, 22, T.grey);
      int px = 120 + (int)constrain(-d * 9, -88.f, 88.f);
      cv.fillSmoothRoundRect(px - 12, 46, 24, 14, 7, lvl ? T.green : T.yel);
      snprintf(b, sizeof(b), "%+.1f", d); textC(110, 76, b, lvl ? T.green : T.white, 3);
      text(150, 76, "o", T.grey);
      textC(120, 22, "EDGE MODE", T.dim, 1, false);
      if (beep && lvl && !wasLevel) Audio::blip(1500, 50, Audio::W_SINE, 0.3f);
      wasLevel = lvl;
    }
    text(124, 96, "LAY IT FLAT, OR", T.dim, 1, false);
    text(124, 106, "STAND IT ON EDGE", T.dim, 1, false);
    hintBar({{"OK", "CALIBRATE"}, {"DEL", "RESET"}, {"S", beep ? "BEEP ON" : "BEEP OFF"}});
  }
};

// ===================================================================== CONVERT
struct Unit { const char* n; double f; };  // value in the base unit = x * f
struct Cat { const char* n; const Unit* u; int nu; };
static const Unit U_LEN[] = {{"MM", 0.001}, {"CM", 0.01}, {"M", 1}, {"KM", 1000}, {"IN", 0.0254}, {"FT", 0.3048}, {"YD", 0.9144}, {"MILE", 1609.344}, {"NMI", 1852}};
static const Unit U_WT[] = {{"MG", 1e-6}, {"G", 0.001}, {"KG", 1}, {"TONNE", 1000}, {"OZ", 0.028349523125}, {"LB", 0.45359237}, {"STONE", 6.35029318}};
static const Unit U_TMP[] = {{"C", 0}, {"F", 0}, {"K", 0}};
static const Unit U_VOL[] = {{"ML", 0.001}, {"L", 1}, {"M3", 1000}, {"TSP", 0.00492892159}, {"TBSP", 0.0147867648}, {"CUP US", 0.2365882365}, {"FL OZ", 0.0295735296}, {"GAL US", 3.785411784}, {"GAL UK", 4.54609}};
static const Unit U_SPD[] = {{"M/S", 1}, {"KM/H", 1 / 3.6}, {"MPH", 0.44704}, {"KNOT", 1852 / 3600.0}, {"FT/S", 0.3048}};
static const Unit U_DAT[] = {{"BIT", 0.125}, {"BYTE", 1}, {"KB", 1e3}, {"MB", 1e6}, {"GB", 1e9}, {"KIB", 1024}, {"MIB", 1048576}, {"GIB", 1073741824.0}};
static const Unit U_AREA[] = {{"CM2", 1e-4}, {"M2", 1}, {"HECTARE", 1e4}, {"KM2", 1e6}, {"IN2", 0.00064516}, {"FT2", 0.09290304}, {"ACRE", 4046.8564224}};
static const Unit U_TIME[] = {{"MS", 0.001}, {"SEC", 1}, {"MIN", 60}, {"HOUR", 3600}, {"DAY", 86400}, {"WEEK", 604800}, {"YEAR", 31557600}};
#define CAT(n, a) {n, a, (int)(sizeof(a) / sizeof(a[0]))}
static const Cat CATS[] = {CAT("LENGTH", U_LEN), CAT("WEIGHT", U_WT), CAT("TEMP", U_TMP), CAT("VOLUME", U_VOL),
                           CAT("SPEED", U_SPD), CAT("DATA", U_DAT), CAT("AREA", U_AREA), CAT("TIME", U_TIME)};
static constexpr int NCAT = sizeof(CATS) / sizeof(CATS[0]);

struct ConvertApp : App {
  int cat = 0, from = 2;
  String in = "1";
  bool fresh = true;  // the first digit typed replaces the starting value
  ConvertApp() { name = "CONVERT"; }
  bool wantsAllKeys() override { return true; }
  bool ownsPlusMinus() override { return true; }  // '-' types a minus sign
  double toBase(double v, int u) {
    if (cat == 2) return u == 0 ? v : u == 1 ? (v - 32) * 5 / 9 : v - 273.15;  // celsius
    return v * CATS[cat].u[u].f;
  }
  double fromBase(double b, int u) {
    if (cat == 2) return u == 0 ? b : u == 1 ? b * 9 / 5 + 32 : b + 273.15;
    return b / CATS[cat].u[u].f;
  }
  static String fmt(double v) {
    char b[24];
    if (v != 0 && (fabs(v) >= 1e9 || fabs(v) < 1e-4)) snprintf(b, sizeof(b), "%.4e", v);
    else {
      snprintf(b, sizeof(b), "%.6f", v);
      char* e = b + strlen(b) - 1;                 // trim trailing zeros
      while (e > b && *e == '0') *e-- = 0;
      if (*e == '.') *e = 0;
      if (strlen(b) > 13) snprintf(b, sizeof(b), "%.6g", v);
    }
    return String(b);
  }
  bool key(const KeyEv& e) override {
    // '.' is a decimal point here; arrows: ; up, FN+. down, , / kind
    KeyCode k = e.code == K_CHAR && e.ch == '.' && !e.fn ? K_NONE : e.nav();
    int nu = CATS[cat].nu;
    if (e.code == K_TAB || (e.code == K_CHAR && (e.ch == ']' || e.ch == '['))) {
      cat = (cat + (e.code == K_CHAR && e.ch == '[' ? NCAT - 1 : 1)) % NCAT;
      from = std::min(from, CATS[cat].nu - 1);
      if (cat == 0) from = 2;
      return true;
    }
    if (k == K_UP) { from = (from + nu - 1) % nu; return true; }
    if (k == K_DOWN) { from = (from + 1) % nu; return true; }
    if (k == K_LEFT || k == K_RIGHT) { cat = (cat + (k == K_LEFT ? NCAT - 1 : 1)) % NCAT; from = std::min(from, CATS[cat].nu - 1); return true; }
    if (e.code == K_BACK) { fresh = false; if (in.length()) in.remove(in.length() - 1); return true; }
    if (e.code == K_CHAR) {
      char c = e.ch;
      if (fresh && (isdigit((unsigned char)c) || c == '.')) { in = ""; fresh = false; }
      if (isdigit((unsigned char)c) && in.length() < 12) { if (in == "0") in = ""; in += c; return true; }
      if (c == '.' && in.indexOf('.') < 0) { in += in.length() ? "." : "0."; return true; }
      if (c == '-' || c == '_') { if (in.startsWith("-")) in.remove(0, 1); else in = "-" + in; return true; }
      if (tolower(c) == 'c') { in = ""; return true; }
    }
    return false;
  }
  void draw() override {
    background();
    statusBar("CONVERT", T.yel);
    const Cat& C = CATS[cat];
    // category tabs
    int x = 3, first = 0;
    auto span = [&](int a, int b) { int w = 0; for (int i = a; i <= b; i++) w += textW(CATS[i].n) + 5; return w; };
    while (first < cat && span(first, cat) > 232) first++;  // scroll so the chosen kind is visible
    for (int i = first; i < NCAT; i++) {
      const char* n = CATS[i].n;
      int w = textW(n) + 4;
      if (i == cat) { cv.fillRect(x - 1, 13, w, 10, lerpc(T.panel, T.yel, 0.3f)); frame(x - 1, 13, w, 10, T.yel); }
      text(x + 1, 14, n, i == cat ? T.white : T.dim, 1, false);
      x += w + 1;
      if (x > 236) break;
    }
    // input
    double v = atof(in.c_str());
    panel(3, 25, 234, 22, T.yel, T.bg1);
    String shown = in.length() ? in : "0";
    text(9, 29, shown.c_str(), T.white, 2);
    if ((millis() / 400) % 2) cv.fillRect(9 + textW(shown.c_str(), 2) + 2, 29, 2, 14, T.yel);
    text(234 - textW(C.u[from].n, 2), 29, C.u[from].n, T.yel, 2);
    // results
    double base = toBase(v, from);
    int rows = C.nu, colW = 117, top = 50;
    for (int i = 0; i < rows; i++) {
      int cx = 3 + (i % 2) * colW, cy = top + (i / 2) * 15;
      bool s = i == from;
      if (s) frame(cx, cy - 2, colW - 3, 14, T.yel);
      String r = fmt(fromBase(base, i));
      textFit(cx + 4, cy + 1, r.c_str(), s ? T.yel : T.white, colW - 12 - textW(C.u[i].n), false);
      text(cx + colW - 8 - textW(C.u[i].n), cy + 1, C.u[i].n, s ? T.yel : T.grey, 1, false);
    }
    hintBar({{"0-9.", "TYPE"}, {"^ FN+v", "UNIT"}, {"<>", "KIND"}, {"C", "CLEAR"}});
  }
};

App* appLevel = new LevelApp();
App* appConvert = new ConvertApp();
