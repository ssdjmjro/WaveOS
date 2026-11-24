// WAVE OS - tools: expression parser, scientific calculator, graphing calculator, resistor calculator
#include "common.h"
#include "audio.h"
#include <math.h>
#include <algorithm>

// ===================================================================== expression parser
namespace Expr {
bool deg = false;
double ans = 0;
struct P {
  const char* s; int i; double x; bool ok; int err;
  char pk() { while (s[i] == ' ') i++; return s[i]; }
  void fail() { if (ok) { ok = false; err = i; } }
};
static double expr(P& p);
static double unary(P& p);
static double trigIn(double v) { return deg ? v * M_PI / 180 : v; }
static double trigOut(double v) { return deg ? v * 180 / M_PI : v; }
static double primary(P& p) {
  char c = p.pk();
  double v = NAN;
  if (isdigit((unsigned char)c) || c == '.') {
    char* end;
    v = strtod(p.s + p.i, &end);
    p.i = end - p.s;
  } else if (c == '(') {
    p.i++; v = expr(p);
    if (p.pk() == ')') p.i++; else p.fail();
  } else if (c == '|') {
    p.i++; v = fabs(expr(p));
    if (p.pk() == '|') p.i++; else p.fail();
  } else if (isalpha((unsigned char)c)) {
    char id[8]; int n = 0;
    while (isalpha((unsigned char)p.s[p.i]) && n < 7) id[n++] = tolower(p.s[p.i++]);
    id[n] = 0;
    if (!strcmp(id, "x")) v = p.x;
    else if (!strcmp(id, "pi")) v = M_PI;
    else if (!strcmp(id, "e")) v = M_E;
    else if (!strcmp(id, "ans")) v = ans;
    else {
      double a;
      if (p.pk() == '(') { p.i++; a = expr(p); if (p.pk() == ')') p.i++; else p.fail(); }
      else a = unary(p);
      if (!strcmp(id, "sin")) v = sin(trigIn(a));
      else if (!strcmp(id, "cos")) v = cos(trigIn(a));
      else if (!strcmp(id, "tan")) v = tan(trigIn(a));
      else if (!strcmp(id, "asin")) v = trigOut(asin(a));
      else if (!strcmp(id, "acos")) v = trigOut(acos(a));
      else if (!strcmp(id, "atan")) v = trigOut(atan(a));
      else if (!strcmp(id, "sqrt")) v = sqrt(a);
      else if (!strcmp(id, "abs")) v = fabs(a);
      else if (!strcmp(id, "ln")) v = log(a);
      else if (!strcmp(id, "log")) v = log10(a);
      else if (!strcmp(id, "exp")) v = exp(a);
      else if (!strcmp(id, "floor")) v = floor(a);
      else if (!strcmp(id, "ceil")) v = ceil(a);
      else if (!strcmp(id, "round")) v = round(a);
      else p.fail();
    }
  } else {
    p.fail();
  }
  while (p.pk() == '!') {  // factorial
    p.i++;
    double r = 1;
    for (int k = 2; k <= (int)v && k < 171; k++) r *= k;
    v = r;
  }
  return v;
}
static double power(P& p) {
  double b = primary(p);
  if (p.pk() == '^') { p.i++; return pow(b, unary(p)); }
  return b;
}
static double unary(P& p) {
  char c = p.pk();
  if (c == '-') { p.i++; return -unary(p); }
  if (c == '+') { p.i++; return unary(p); }
  return power(p);
}
static double term(P& p) {
  double v = unary(p);
  for (;;) {
    char c = p.pk();
    if (c == '*') { p.i++; v *= unary(p); }
    else if (c == '/') { p.i++; v /= unary(p); }
    else if (c == '%') { p.i++; v = fmod(v, unary(p)); }
    else if (c == '(' || isalpha((unsigned char)c) || c == '|') { v *= unary(p); }  // implicit: 2x, 3(x+1)
    else break;
  }
  return v;
}
static double expr(P& p) {
  double v = term(p);
  for (;;) {
    char c = p.pk();
    if (c == '+') { p.i++; v += term(p); }
    else if (c == '-') { p.i++; v -= term(p); }
    else break;
  }
  return v;
}
double eval(const char* s, double x, bool* ok, int* err = nullptr) {
  // accept "f(x)=..." / "y=..."
  const char* eq = strchr(s, '=');
  if (eq) s = eq + 1;
  P p{s, 0, x, true, -1};
  double v = expr(p);
  if (p.pk() != 0) p.fail();
  if (ok) *ok = p.ok;
  if (err) *err = p.err;
  return v;
}
String fmt(double v) {
  if (isnan(v)) return "UNDEFINED";
  if (isinf(v)) return v > 0 ? "INF" : "-INF";
  char b[32];
  if (fabs(v) >= 1e10 || (fabs(v) < 1e-6 && v != 0)) snprintf(b, sizeof(b), "%.6g", v);
  else {
    snprintf(b, sizeof(b), "%.10f", v);
    char* d = strchr(b, '.');
    if (d) { char* e = b + strlen(b) - 1; while (e > d && *e == '0') *e-- = 0; if (e == d) *e = 0; }
  }
  return String(b);
}
}  // namespace Expr

// ===================================================================== line editor helper
struct LineEdit {
  String s;
  int cur = 0;
  bool handle(const KeyEv& e) {
    KeyCode k = e.navFn();
    if (k == K_LEFT) { cur = std::max(0, cur - 1); return true; }
    if (k == K_RIGHT) { cur = std::min((int)s.length(), cur + 1); return true; }
    if (e.code == K_BACK) {
      if (e.fn) { if (cur < (int)s.length()) s.remove(cur, 1); }
      else if (cur > 0) { s.remove(cur - 1, 1); cur--; }
      return true;
    }
    if (e.code == K_CHAR && !e.fn && e.ch >= 32 && e.ch < 127 && s.length() < 60) {
      s = s.substring(0, cur) + e.ch + s.substring(cur);
      cur++;
      return true;
    }
    return false;
  }
  void set(const String& v) { s = v; cur = v.length(); }
  void draw(int x, int y, int w, uint16_t c, bool caret) {
    int maxc = w / 6;
    int start = std::max(0, cur - maxc + 1);
    String vis = s.substring(start, start + maxc);
    text(x, y, vis.c_str(), c, 1, false);
    if (caret && (millis() / 400) % 2 == 0) cv.fillRect(x + (cur - start) * 6, y + 8, 5, 1, T.cyan);
  }
};

// ===================================================================== calculator
struct CalcApp : App {
  LineEdit in;
  std::vector<String> histE, histR;
  int recall = -1;
  bool help = false;
  CalcApp() { name = "CALC"; }
  bool wantsAllKeys() override { return true; }
  bool key(const KeyEv& e) override {
    if (help) { help = false; return true; }
    KeyCode k = e.navFn();
    if (e.code == K_TAB) { Expr::deg = !Expr::deg; toast(Expr::deg ? "DEGREES" : "RADIANS"); return true; }
    if (k == K_UP && !histE.empty()) { recall = recall < 0 ? histE.size() - 1 : std::max(0, recall - 1); in.set(histE[recall]); return true; }
    if (k == K_DOWN && recall >= 0) { recall++; if (recall >= (int)histE.size()) { recall = -1; in.set(""); } else in.set(histE[recall]); return true; }
    if (e.code == K_ENTER) {
      if (e.fn) { sendToGraph(in.s.length() ? in.s.c_str() : (histE.empty() ? "x" : histE.back().c_str())); return true; }
      if (!in.s.length()) { if (!histE.empty()) in.set(histE.back()); else return true; }
      bool ok; int err;
      double v = Expr::eval(in.s.c_str(), 0, &ok, &err);
      histE.push_back(in.s);
      if (ok) { Expr::ans = v; histR.push_back(Expr::fmt(v)); Audio::blip(1320, 30, Audio::W_SINE, 0.3f); }
      else { histR.push_back("ERROR"); Audio::blip(220, 80, Audio::W_SQUARE, 0.3f); }
      if (histE.size() > 30) { histE.erase(histE.begin()); histR.erase(histR.begin()); }
      in.set(""); recall = -1;
      return true;
    }
    if (e.fn && e.code == K_CHAR && tolower(e.ch) == 'c') { histE.clear(); histR.clear(); in.set(""); return true; }
    if (e.fn && e.code == K_CHAR && (e.ch == 'h' || e.ch == 'H')) { help = true; return true; }
    if (e.code == K_CHAR && !e.fn && in.s.length() == 0 && strchr("+-*/^%", e.ch) && !histE.empty()) {
      in.set("ans");  // continue from previous answer
    }
    return in.handle(e);
  }
  void draw() override {
    background(true);
    statusBar("CALC", T.yel);
    text(190, 2, Expr::deg ? "DEG" : "RAD", T.grey, 1, false);
    panel(3, 14, 234, 80, T.edge, T.bg0);
    int n = histE.size();
    int rows = 4;
    for (int r = 0; r < rows && r < n; r++) {
      int i = n - 1 - r;
      int y = 76 - r * 19;
      textFit(8, y - 8, histE[i].c_str(), T.grey, 220, false);
      const String& res = histR[i];
      text(232 - textW(res.c_str()), y + 1, res.c_str(), res == "ERROR" ? T.red : (r == 0 ? T.white : T.grey), 1, r == 0);
    }
    if (!n) { textC(120, 40, "TYPE AN EXPRESSION", T.grey); textC(120, 52, "2^10, SQRT(2), SIN(PI/2), 5!", T.dim, 1, false); }
    panel(3, 96, 234, 16, T.yel, T.panel);
    text(7, 100, ">", T.yel, 1, false);
    in.draw(15, 100, 216, T.white, true);
    // programmer view of last answer
    if (n && histR.back() != "ERROR" && fabs(Expr::ans) < 4294967296.0 && Expr::ans == floor(Expr::ans)) {
      char b[48];
      long long v = (long long)Expr::ans;
      snprintf(b, sizeof(b), "HEX %llX", (unsigned long long)(v < 0 ? (uint32_t)v : v));
      text(7, 114, b, T.purp, 1, false);
      String bin;
      uint32_t u = (uint32_t)v;
      for (int i = 15; i >= 0; i--) bin += (u >> i) & 1 ? '1' : '0';
      text(100, 114, ("BIN " + bin).c_str(), T.dim, 1, false);
      cv.fillRect(0, 125, 240, 10, T.bar);
    }
    hintBar({{"OK", "="}, {"FN^", "HIST"}, {"TAB", "DEG/RAD"}, {"FN+OK", "GRAPH"}});
    if (help) helpOverlay("CALC", {"OK  EVALUATE", "FN+; .  HISTORY", "FN+, /  CURSOR", "TAB  DEG/RAD", "FN+C  CLEAR", "FN+OK  TO GRAPH",
                                   "ANS  LAST RESULT", "FUNCS", "SIN COS TAN", "ASIN ACOS ATAN", "SQRT ABS LN LOG", "EXP FLOOR CEIL", "PI E X !"});
  }
};

// ===================================================================== graph
struct GraphApp : App {
  LineEdit fx[2];
  int editing = -1;  // -1 view mode, 0 f, 1 g
  int traceFn = 0;
  double xmin = -5, xmax = 5, ymin = -3, ymax = 3;
  int tx = 120;
  bool dirty = true;
  float ys[2][240];
  bool valid[2] = {false, false};
  const int GX = 0, GY = 12, GW = 240, GH = 113;
  GraphApp() { name = "GRAPH"; fx[0].set("2x^2+x^3"); fx[1].set(""); }
  bool wantsAllKeys() override { return editing >= 0; }
  double px2x(int px) { return xmin + (xmax - xmin) * px / (GW - 1); }
  int y2py(double y) { return GY + (int)((ymax - y) / (ymax - ymin) * (GH - 1)); }
  void recompute() {
    for (int f = 0; f < 2; f++) {
      valid[f] = fx[f].s.length() > 0;
      if (!valid[f]) continue;
      bool ok = true;
      for (int i = 0; i < GW && ok; i++) ys[f][i] = Expr::eval(fx[f].s.c_str(), px2x(i), &ok);
      if (!ok) valid[f] = false;
    }
    dirty = false;
  }
  void zoom(double k) {
    double cx = (xmin + xmax) / 2, cy = (ymin + ymax) / 2;
    double hw = (xmax - xmin) / 2 * k, hh = (ymax - ymin) / 2 * k;
    xmin = cx - hw; xmax = cx + hw; ymin = cy - hh; ymax = cy + hh; dirty = true;
  }
  void jumpExtremum() {
    if (!valid[traceFn]) return;
    float* y = ys[traceFn];
    int best = -1, bd = 999;
    for (int i = 1; i < GW - 1; i++)
      if (((y[i] >= y[i - 1] && y[i] > y[i + 1]) || (y[i] <= y[i - 1] && y[i] < y[i + 1])) && abs(i - tx) < bd && abs(i - tx) > 0) { bd = abs(i - tx); best = i; }
    if (best >= 0) tx = best; else toast("NO MAX/MIN IN VIEW");
  }
  void jumpZero() {
    if (!valid[traceFn]) return;
    float* y = ys[traceFn];
    int best = -1, bd = 999;
    for (int i = 1; i < GW; i++)
      if ((y[i - 1] <= 0) != (y[i] <= 0) && abs(i - tx) < bd && abs(i - tx) > 0) { bd = abs(i - tx); best = i; }
    if (best >= 0) tx = best; else toast("NO ZERO IN VIEW");
  }
  bool key(const KeyEv& e) override {
    if (editing >= 0) {
      if (e.code == K_ENTER || e.code == K_ESC) { editing = -1; dirty = true; return true; }
      if (e.code == K_TAB) { editing = 1 - editing; return true; }
      return fx[editing].handle(e);
    }
    KeyCode k = e.nav();
    double dx = (xmax - xmin), dy = (ymax - ymin);
    if (k == K_LEFT) { tx -= e.rep ? 4 : 1; if (tx < 0) { tx = 0; xmin -= dx / 10; xmax -= dx / 10; dirty = true; } return true; }
    if (k == K_RIGHT) { tx += e.rep ? 4 : 1; if (tx >= GW) { tx = GW - 1; xmin += dx / 10; xmax += dx / 10; dirty = true; } return true; }
    if (k == K_UP) { ymin += dy / 10; ymax += dy / 10; return true; }
    if (k == K_DOWN) { ymin -= dy / 10; ymax -= dy / 10; return true; }
    if (k == K_ENTER || e.is('e')) { editing = traceFn; return true; }
    if (e.code == K_TAB) { traceFn = valid[1 - traceFn] ? 1 - traceFn : traceFn; return true; }
    if (e.code != K_CHAR) return false;
    switch (tolower(e.ch)) {
      case '-': case '_': zoom(1.25); return true;
      case '=': case '+': zoom(0.8); return true;
      case '[': xmin -= dx / 10; xmax -= dx / 10; dirty = true; return true;
      case ']': xmin += dx / 10; xmax += dx / 10; dirty = true; return true;
      case '0': xmin = -5; xmax = 5; ymin = -3; ymax = 3; dirty = true; return true;
      case 'm': jumpExtremum(); return true;
      case 'z': jumpZero(); return true;
      case 'g': editing = 1; return true;
      case 'f': editing = 0; return true;
    }
    return false;
  }
  void draw() override {
    if (dirty) recompute();
    background(true);
    // grid with "nice" step
    double span = xmax - xmin;
    double step = pow(10, floor(log10(span / 5)));
    if (span / step > 12) step *= 2;
    if (span / step > 12) step *= 2.5;
    for (double gx = ceil(xmin / step) * step; gx <= xmax; gx += step) {
      int px = (int)((gx - xmin) / span * (GW - 1));
      for (int y = GY; y < GY + GH; y += 2) cv.drawPixel(px, y, T.bg2);
    }
    double yspan = ymax - ymin;
    double ystep = pow(10, floor(log10(yspan / 4)));
    if (yspan / ystep > 10) ystep *= 2;
    if (yspan / ystep > 10) ystep *= 2.5;
    for (double gy = ceil(ymin / ystep) * ystep; gy <= ymax; gy += ystep) {
      int py = y2py(gy);
      for (int x = 0; x < GW; x += 2) cv.drawPixel(x, py, T.bg2);
    }
    int ox = (int)((0 - xmin) / span * (GW - 1)), oy = y2py(0);
    if (oy >= GY && oy < GY + GH) cv.drawFastHLine(0, oy, GW, T.dim);
    if (ox >= 0 && ox < GW) cv.drawFastVLine(ox, GY, GH, T.dim);
    // axis numbers
    for (double gx = ceil(xmin / step) * step; gx <= xmax; gx += step) {
      if (fabs(gx) < step / 2) continue;
      int px = (int)((gx - xmin) / span * (GW - 1));
      String s = Expr::fmt(gx);
      int ly = constrain(oy + 3, GY + 2, GY + GH - 9);
      if (s.length() < 6) text(px - textW(s.c_str()) / 2, ly, s.c_str(), T.dim, 1, false);
    }
    // curves
    uint16_t cols[2] = {T.yel, T.cyan};
    for (int f = 0; f < 2; f++) {
      if (!valid[f]) continue;
      int prev = INT32_MIN;
      for (int i = 0; i < GW; i++) {
        float y = ys[f][i];
        if (isnan(y) || isinf(y)) { prev = INT32_MIN; continue; }
        int py = y2py(y);
        if (prev != INT32_MIN && abs(py - prev) < GH * 3) {
          int a = constrain(std::min(prev, py), GY - 1, GY + GH), b = constrain(std::max(prev, py), GY - 1, GY + GH);
          for (int yy = a; yy <= b; yy++) {
            if (yy < GY || yy >= GY + GH) continue;
            cv.drawPixel(i, yy, cols[f]);
            cv.drawPixel(i - 1, yy, lerpc(T.bg0, cols[f], 0.25f));
            cv.drawPixel(i + 1, yy, lerpc(T.bg0, cols[f], 0.25f));
          }
        } else if (py >= GY && py < GY + GH) cv.drawPixel(i, py, cols[f]);
        prev = py;
      }
    }
    // trace
    if (valid[traceFn] && editing < 0) {
      float y = ys[traceFn][tx];
      for (int yy = GY; yy < GY + GH; yy += 2) cv.drawPixel(tx, yy, T.purpD);
      int py = y2py(y);
      if (py >= GY && py < GY + GH) { frame(tx - 3, py - 3, 7, 7, T.cyan); cv.drawPixel(tx, py, T.white); }
    }
    statusBar("GRAPH", T.yel);
    // formula panel
    int ph = editing >= 0 ? 32 : (valid[0] && valid[1] ? 32 : 22);
    panel(3, 14, 130, ph, editing >= 0 ? T.cyan : T.yel, T.panel);
    for (int f = 0; f < 2; f++) {
      if (f == 1 && editing < 0 && !valid[1]) break;
      int y = 18 + f * 10;
      text(7, y, f == 0 ? "F=" : "G=", cols[f], 1, false);
      if (editing == f) fx[f].draw(20, y, 110, T.white, true);
      else textFit(20, y, fx[f].s.length() ? fx[f].s.c_str() : "-", T.white, 110, false);
    }
    if (editing < 0 && valid[traceFn]) {
      char b[48];
      snprintf(b, sizeof(b), "X=%s Y=%s", Expr::fmt(round(px2x(tx) * 1000) / 1000).c_str(), Expr::fmt(round(ys[traceFn][tx] * 1000) / 1000).c_str());
      int y = 14 + ph + 2;
      cv.fillRect(3, y, textW(b) + 8, 11, T.bar);
      text(7, y + 2, b, T.cyan, 1, false);
    }
    if (editing >= 0) hintBar({{"OK", "PLOT"}, {"TAB", "F/G"}, {"^", "POW"}, {"FN<>", "CURSOR"}});
    else hintBar({{"<>", "TRACE"}, {"-=", "ZOOM"}, {"M", "MAX/MIN"}, {"Z", "ZERO"}, {"OK", "EDIT"}});
  }
};

// ===================================================================== resistor
struct ResistorApp : App {
  // band colour index 0..11 : black brown red orange yellow green blue violet grey white gold silver
  static uint16_t col(int i) {
    static const uint16_t c[12] = {RGB(25, 25, 25), RGB(150, 80, 30), RGB(220, 40, 40), RGB(255, 120, 0), RGB(250, 220, 40), RGB(40, 180, 60),
                                   RGB(40, 90, 230), RGB(150, 70, 210), RGB(130, 130, 130), RGB(245, 245, 245), RGB(210, 170, 40), RGB(190, 195, 200)};
    return c[i];
  }
  static const char* cname(int i) {
    static const char* n[12] = {"BLACK", "BROWN", "RED", "ORANGE", "YELLOW", "GREEN", "BLUE", "VIOLET", "GREY", "WHITE", "GOLD", "SILVER"};
    return n[i];
  }
  int mode = 0;  // 0 4-band, 1 5-band, 2 value -> bands
  int b4[4] = {1, 0, 3, 10}, b5[5] = {1, 0, 0, 2, 1};
  int sel = 0;
  LineEdit in;
  double found = -1;
  ResistorApp() { name = "RESISTOR"; }
  bool wantsAllKeys() override { return mode == 2; }

  // which colours are legal per band
  bool legal(int band, int c, int n) {
    if (band < n - 2) return c <= 9 && !(band == 0 && c == 0);
    if (band == n - 2) return c <= 7 || c >= 10;  // multiplier
    return c == 1 || c == 2 || c == 5 || c == 6 || c == 7 || c == 8 || c == 10 || c == 11;  // tolerance
  }
  double mult(int c) { return c == 10 ? 0.1 : (c == 11 ? 0.01 : pow(10, c)); }
  double tol(int c) {
    switch (c) { case 1: return 1; case 2: return 2; case 5: return 0.5; case 6: return 0.25; case 7: return 0.1; case 8: return 0.05; case 10: return 5; default: return 10; }
  }
  double value(int* b, int n) {
    double d = 0;
    for (int i = 0; i < n - 2; i++) d = d * 10 + b[i];
    return d * mult(b[n - 2]);
  }
  static String ohms(double v) {
    char b[24];
    const char* u = "";
    if (v >= 1e6) { v /= 1e6; u = "M"; } else if (v >= 1e3) { v /= 1e3; u = "K"; }
    snprintf(b, sizeof(b), "%g%s", round(v * 100) / 100, u);
    return String(b);
  }
  double parse(String s) {
    s.trim(); s.toLowerCase();
    double m = 1;
    int p = -1;
    for (int i = 0; i < (int)s.length(); i++) if (s[i] == 'k' || s[i] == 'm' || s[i] == 'r') { p = i; m = s[i] == 'k' ? 1e3 : (s[i] == 'm' ? 1e6 : 1); break; }
    if (p >= 0) {  // 4k7 style
      String a = s.substring(0, p), b = s.substring(p + 1);
      double v = a.length() ? a.toDouble() : 0;
      if (b.length()) v += b.toDouble() / pow(10, b.length());
      return v * m;
    }
    return s.toDouble();
  }
  void solve(double v) {
    // nearest E24 value
    static const double e24[] = {1.0, 1.1, 1.2, 1.3, 1.5, 1.6, 1.8, 2.0, 2.2, 2.4, 2.7, 3.0, 3.3, 3.6, 3.9, 4.3, 4.7, 5.1, 5.6, 6.2, 6.8, 7.5, 8.2, 9.1};
    int ex = (int)floor(log10(v));
    double mant = v / pow(10, ex), best = 1, bd = 1e9;
    for (double c : e24) if (fabs(c - mant) < bd) { bd = fabs(c - mant); best = c; }
    if (fabs(10 - mant) < bd) { best = 1; ex++; }
    found = best * pow(10, ex);
    int d1 = (int)best, d2 = (int)round(best * 10) % 10;
    int m = ex - 1;
    if (m >= -2 && m <= 7) { b4[0] = d1; b4[1] = d2; b4[2] = m == -1 ? 10 : (m == -2 ? 11 : m); b4[3] = 10; }
    long long three = llround(v * 100 / pow(10, ex));  // 3 significant digits
    if (three >= 1000) { three /= 10; ex++; }
    int m5 = ex - 2;
    if (m5 >= -2 && m5 <= 7) {
      b5[0] = three / 100; b5[1] = (three / 10) % 10; b5[2] = three % 10; b5[3] = m5 == -1 ? 10 : (m5 == -2 ? 11 : m5); b5[4] = 1;
    }
  }
  bool key(const KeyEv& e) override {
    if (e.code == K_TAB) { mode = (mode + 1) % 3; sel = 0; return true; }
    if (mode == 2) {
      if (e.code == K_ENTER) { double v = parse(in.s); if (v > 0 && v < 1e10) solve(v); else toast("TYPE A VALUE LIKE 4K7"); return true; }
      if (e.code == K_ESC) return false;
      return in.handle(e);
    }
    int n = mode == 0 ? 4 : 5;
    int* b = mode == 0 ? b4 : b5;
    KeyCode k = e.nav();
    if (k == K_LEFT) { sel = (sel + n - 1) % n; return true; }
    if (k == K_RIGHT) { sel = (sel + 1) % n; return true; }
    if (k == K_UP || k == K_DOWN) {
      int d = k == K_UP ? 1 : -1;
      int c = b[sel];
      for (int t = 0; t < 12; t++) { c = (c + d + 12) % 12; if (legal(sel, c, n)) break; }
      b[sel] = c;
      return true;
    }
    return false;
  }
  void drawResistor(int* b, int n, int selBand) {
    for (int x = 10; x < 60; x++) { cv.drawPixel(x, 44, T.grey); cv.drawPixel(x, 45, T.dim); }
    for (int x = 180; x < 230; x++) { cv.drawPixel(x, 44, T.grey); cv.drawPixel(x, 45, T.dim); }
    uint16_t body = RGB(214, 190, 150);
    for (int x = 60; x < 180; x++) {
      int bulge = (x < 76 || x > 164) ? 0 : 4;
      for (int y = 28 + bulge; y < 62 - bulge; y++) {
        float t = (y - 28) / 34.f;
        cv.drawPixel(x, y, t < 0.5f ? lerpc(lerpc(body, T.white, 0.35f), body, t * 2) : lerpc(body, lerpc(body, T.bg0, 0.45f), (t - 0.5f) * 2));
      }
    }
    int pos4[4] = {82, 100, 118, 156}, pos5[5] = {80, 96, 112, 128, 158};
    for (int i = 0; i < n; i++) {
      int bx = n == 4 ? pos4[i] : pos5[i];
      uint16_t bc = col(b[i]);
      for (int x = bx; x < bx + 9; x++) {
        int y0 = (x < 76 || x > 164) ? 28 : 32, y1 = (x < 76 || x > 164) ? 62 : 58;
        for (int y = y0; y < y1; y++) {
          float t = (y - 28) / 34.f;
          cv.drawPixel(x, y, t < 0.4f ? lerpc(bc, T.white, 0.3f * (0.4f - t) / 0.4f) : lerpc(bc, T.bg0, (t - 0.4f) * 0.8f));
        }
      }
      if (i == selBand) {
        glowFrame(bx - 2, 24, 13, 42, T.cyan);
        for (int j = 0; j < 3; j++) cv.drawFastHLine(bx + 2 + j, 18 + j, 5 - 2 * j, T.cyan);
      }
    }
  }
  void draw() override {
    background();
    statusBar("RESISTOR", T.yel);
    const char* mn[3] = {"4-BAND", "5-BAND", "FIND BANDS"};
    text(150, 2, mn[mode], T.purp, 1, false);
    if (mode < 2) {
      int n = mode == 0 ? 4 : 5;
      int* b = mode == 0 ? b4 : b5;
      drawResistor(b, n, sel);
      for (int i = 0; i < n; i++) {
        int x = 6 + i * (228 / n);
        text(x, 70, cname(b[i]), i == sel ? T.cyan : T.grey, 1, false);
      }
      double v = value(b, n), t = tol(b[n - 1]);
      panel(30, 82, 180, 40, T.yel);
      String s = ohms(v);
      int w = textW(s.c_str(), 2) + 14;
      char tb[12]; snprintf(tb, sizeof(tb), "%g%%", t);
      int tw = textW(tb, 2);
      int x0 = 120 - (w + 12 + tw) / 2;
      text(x0, 86, s.c_str(), T.white, 2);
      drawOhm(x0 + w - 12, 86, T.white, 2);
      text(x0 + w + 12, 86, tb, T.yel, 2);
      char r[48];
      snprintf(r, sizeof(r), "%s - %s", ohms(v * (1 - t / 100)).c_str(), ohms(v * (1 + t / 100)).c_str());
      textC(120, 107, r, T.grey, 1, false);
      hintBar({{"<>", "BAND"}, {"^v", "COLOR"}, {"TAB", "MODE"}});
    } else {
      panel(6, 16, 228, 20, T.yel);
      text(10, 22, "VALUE:", T.yel, 1, false);
      in.draw(52, 22, 170, T.white, true);
      if (found > 0) {
        text(10, 42, "NEAREST E24:", T.grey, 1, false);
        text(90, 42, (ohms(found)).c_str(), T.white, 1, false);
        drawOhm(90 + textW(ohms(found).c_str()) + 2, 42, T.white);
        text(10, 56, "4-BAND", T.purp, 1, false);
        text(10, 88, "5-BAND", T.purp, 1, false);
        for (int i = 0; i < 4; i++) {
          cv.fillRect(56 + i * 44, 54, 40, 11, col(b4[i]));
          text(58 + i * 44, 67, cname(b4[i]), T.grey, 1, false);
        }
        for (int i = 0; i < 5; i++) {
          cv.fillRect(56 + i * 35, 86, 32, 11, col(b5[i]));
          textFit(56 + i * 35, 99, cname(b5[i]), T.grey, 34, false);
        }
      } else {
        textC(120, 60, "TYPE 220, 4K7, 1.5M ...", T.grey);
        textC(120, 74, "THEN PRESS OK", T.dim, 1, false);
      }
      hintBar({{"OK", "FIND"}, {"TAB", "MODE"}, {"ESC", "BACK"}});
    }
  }
};

App* appCalc = new CalcApp();
App* appGraph = new GraphApp();
App* appResistor = new ResistorApp();

void sendToGraph(const char* expr) {
  GraphApp* g = (GraphApp*)appGraph;
  g->fx[0].set(expr);
  g->dirty = true;
  g->editing = -1;
  openApp(appGraph);
  toast("SENT TO GRAPH");
}

// ===================================================================== electronics toolbox
static double parseEng(String s) {
  s.trim();
  if (!s.length()) return NAN;
  char last = s[s.length() - 1];
  double m = 1;
  switch (last) {
    case 'p': m = 1e-12; break; case 'n': m = 1e-9; break; case 'u': m = 1e-6; break;
    case 'm': m = 1e-3; break; case 'k': case 'K': m = 1e3; break; case 'M': m = 1e6; break;
  }
  if (m != 1) s.remove(s.length() - 1);
  bool ok;
  double v = Expr::eval(s.c_str(), 0, &ok);
  return ok ? v * m : NAN;
}
static String eng(double v, const char* unit) {
  if (isnan(v) || isinf(v)) return "-";
  const char* pre[] = {"p", "n", "u", "m", "", "k", "M", "G"};
  int i = 4;
  double a = fabs(v);
  while (a >= 1000 && i < 7) { a /= 1000; v /= 1000; i++; }
  while (a < 1 && a > 0 && i > 0) { a *= 1000; v *= 1000; i--; }
  char b[24];
  snprintf(b, sizeof(b), "%.3g%s%s", v, pre[i], unit);
  return String(b);
}
static double nearestE(double v, bool up) {
  static const double e12[] = {1.0, 1.2, 1.5, 1.8, 2.2, 2.7, 3.3, 3.9, 4.7, 5.6, 6.8, 8.2, 10.0};
  if (!(v > 0)) return NAN;
  int ex = (int)floor(log10(v));
  double m = v / pow(10, ex);
  for (double c : e12) if (up ? c >= m - 1e-9 : true) { if (up) return c * pow(10, ex); }
  return 10 * pow(10, ex);
}

struct ElecApp : App {
  enum { OHM, LED, DIV, T555, CAP, NP };
  int page = OHM, sel = 0;
  LineEdit f[NP][3];
  ElecApp() {
    name = "ELEC";
    f[OHM][0].set("5"); f[OHM][1].set(""); f[OHM][2].set("220");
    f[LED][0].set("5"); f[LED][1].set("2"); f[LED][2].set("20m");
    f[DIV][0].set("9"); f[DIV][1].set("10k"); f[DIV][2].set("4.7k");
    f[T555][0].set("1k"); f[T555][1].set("10k"); f[T555][2].set("10u");
    f[CAP][0].set("104");
  }
  bool wantsAllKeys() override { return true; }
  int nFields() { return page == CAP ? 1 : 3; }
  const char* label(int p, int i) {
    static const char* L[NP][3] = {{"VOLTAGE V", "CURRENT A", "RESIST. OHM"}, {"SUPPLY V", "LED VF V", "CURRENT A"},
                                   {"VIN V", "R1 OHM", "R2 OHM"}, {"R1 OHM", "R2 OHM", "C FARAD"}, {"CODE", "", ""}};
    return L[p][i];
  }
  bool key(const KeyEv& e) override {
    if (e.code == K_TAB) { page = (page + 1) % NP; sel = 0; return true; }
    KeyCode k = e.navFn();
    if (k == K_UP || (e.code == K_ENTER && e.shift)) { sel = (sel + nFields() - 1) % nFields(); return true; }
    if (k == K_DOWN || e.code == K_ENTER) { sel = (sel + 1) % nFields(); return true; }
    if (e.code == K_ESC) return false;
    return f[page][sel].handle(e);
  }
  void draw() override {
    background();
    const char* t[NP] = {"OHM'S LAW", "LED RESISTOR", "VOLTAGE DIVIDER", "555 ASTABLE", "CAP CODE"};
    statusBar(t[page], T.yel);
    for (int i = 0; i < NP; i++) cv.fillRect(150 + i * 8, 13, 6, 1, i == page ? T.yel : T.dim);
    int n = nFields();
    for (int i = 0; i < n; i++) {
      int y = 18 + i * 17;
      bool s = i == sel;
      panel(4, y, 232, 15, s ? T.yel : T.edge, s ? lerpc(T.panel, T.yel, 0.1f) : T.panel);
      text(9, y + 4, label(page, i), s ? T.yel : T.grey, 1, false);
      f[page][i].draw(90, y + 4, 140, T.white, s);
    }
    double a = parseEng(f[page][0].s), b = parseEng(f[page][1].s), c = parseEng(f[page][2].s);
    std::vector<String> out;
    switch (page) {
      case OHM:
        if (isnan(a) && !isnan(b) && !isnan(c)) a = b * c;
        else if (isnan(b) && !isnan(a) && !isnan(c)) b = a / c;
        else if (isnan(c) && !isnan(a) && !isnan(b)) c = a / b;
        out.push_back("V = " + eng(a, "V") + "   I = " + eng(b, "A"));
        out.push_back("R = " + eng(c, "OHM"));
        out.push_back("P = " + eng(a * b, "W"));
        out.push_back("LEAVE ONE FIELD EMPTY");
        break;
      case LED: {
        double r = (a - b) / c, re = nearestE(r, true);
        out.push_back("R = " + eng(r, "OHM"));
        out.push_back("USE " + eng(re, "OHM") + " (E12)");
        double p = c * c * re;
        out.push_back("P = " + eng(p, "W") + (p > 0.25 ? "  USE 1/2W!" : "  1/4W OK"));
        out.push_back("REAL I = " + eng((a - b) / re, "A"));
        break;
      }
      case DIV:
        out.push_back("VOUT = " + eng(a * c / (b + c), "V"));
        out.push_back("I = " + eng(a / (b + c), "A"));
        out.push_back("RATIO = " + String(c / (b + c), 3));
        break;
      case T555: {
        double fr = 1.44 / ((a + 2 * b) * c);
        out.push_back("F = " + eng(fr, "HZ"));
        out.push_back("T = " + eng(1 / fr, "S"));
        out.push_back("HIGH " + eng(0.693 * (a + b) * c, "S") + " LOW " + eng(0.693 * b * c, "S"));
        out.push_back("DUTY = " + String(100 * (a + b) / (a + 2 * b), 1) + "%");
        break;
      }
      case CAP: {
        String s = f[CAP][0].s; s.trim();
        if (s.length() >= 3 && isdigit((unsigned char)s[0])) {
          double pf = s.substring(0, 2).toInt() * pow(10, s[2] - '0');
          out.push_back(eng(pf * 1e-12, "F"));
          out.push_back(String(pf, 0) + " PF");
          out.push_back(String(pf / 1000, 3) + " NF / " + String(pf / 1e6, 4) + " UF");
        } else out.push_back("TYPE A 3 DIGIT CODE (104)");
        break;
      }
    }
    int y0 = 20 + n * 17;
    panel(4, y0, 232, 123 - y0, T.edge, T.bg0);
    for (size_t i = 0; i < out.size(); i++) {
      String l = out[i]; l.toUpperCase();
      text(10, y0 + 5 + i * 11, l.c_str(), i == 0 ? T.white : T.cyan, 1, i == 0);
    }
    hintBar({{"^v", "FIELD"}, {"TAB", "PAGE"}, {"K M U N", "UNITS"}});
  }
};
App* appElec = new ElecApp();

// ===================================================================== TIMER: stopwatch + countdown (alarm works in any app)
namespace Tmr {
bool swRun = false; uint32_t swStart = 0, swAcc = 0; std::vector<uint32_t> laps;
bool cdRun = false, ringing = false; int32_t cdSet = 5 * 60 * 1000; uint32_t cdEnd = 0, cdLeftPaused = 5 * 60 * 1000;
uint32_t swNow() { return swAcc + (swRun ? millis() - swStart : 0); }
int32_t cdLeft() { return cdRun ? (int32_t)(cdEnd - millis()) : (int32_t)cdLeftPaused; }
}
void timerTick() {
  using namespace Tmr;
  if (cdRun && (int32_t)(cdEnd - millis()) <= 0) { cdRun = false; cdLeftPaused = 0; ringing = true; }
  static uint32_t lastBeep = 0, ringStart = 0;
  if (!ringing) { ringStart = 0; return; }
  if (!ringStart) ringStart = millis();
  if ((!heldKeys.empty() && millis() - ringStart > 500) || millis() - ringStart > 60000) { ringing = false; cdLeftPaused = cdSet; return; }  // any key stops it
  if (millis() - lastBeep > 600) { lastBeep = millis(); Audio::blip(1760, 120, Audio::W_SQUARE, 0.5f); toast("TIMER DONE! PRESS ANY KEY", 700); }
}
struct TimerApp : App {
  int page = 0;  // 0 stopwatch, 1 countdown
  TimerApp() { name = "TIMER"; }
  static String fmt(uint32_t ms, bool cs) {
    char b[20];
    uint32_t s = ms / 1000;
    if (cs) snprintf(b, sizeof(b), "%02u:%02u.%02u", (unsigned)(s / 60), (unsigned)(s % 60), (unsigned)((ms / 10) % 100));
    else if (s >= 3600) snprintf(b, sizeof(b), "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)((s / 60) % 60), (unsigned)(s % 60));
    else snprintf(b, sizeof(b), "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    return String(b);
  }
  bool key(const KeyEv& e) override {
    using namespace Tmr;
    if (ringing) { ringing = false; cdLeftPaused = cdSet; return true; }
    if (e.code == K_TAB) { page = 1 - page; return true; }
    KeyCode k = e.nav();
    if (page == 0) {
      if (e.code == K_CHAR && e.ch == ' ') { if (swRun) { swAcc += millis() - swStart; swRun = false; } else { swStart = millis(); swRun = true; } return true; }
      if (e.is('l') && swRun) { laps.insert(laps.begin(), swNow()); if (laps.size() > 4) laps.pop_back(); return true; }
      if (e.code == K_BACK) { swRun = false; swAcc = 0; laps.clear(); return true; }
      return false;
    }
    if (e.code == K_CHAR && e.ch == ' ') {
      if (cdRun) { cdLeftPaused = cdLeft(); cdRun = false; }
      else if (cdLeftPaused > 0) { cdEnd = millis() + cdLeftPaused; cdRun = true; }
      return true;
    }
    if (cdRun) return false;
    int32_t d = 0;
    if (k == K_UP) d = 60000; else if (k == K_DOWN) d = -60000; else if (k == K_RIGHT) d = 10000; else if (k == K_LEFT) d = -10000;
    if (e.code == K_CHAR && e.ch >= '1' && e.ch <= '9') { cdSet = (e.ch - '0') * (e.ch == '9' ? 10 : e.ch >= '6' ? 5 : 1) * 60000; if (e.ch == '6') cdSet = 15 * 60000; if (e.ch == '7') cdSet = 25 * 60000; if (e.ch == '8') cdSet = 45 * 60000; cdLeftPaused = cdSet; return true; }
    if (d) { cdSet = constrain(cdSet + d, 10000, 6 * 3600000); cdLeftPaused = cdSet; return true; }
    if (e.code == K_BACK) { cdLeftPaused = cdSet; return true; }
    return false;
  }
  void draw() override {
    using namespace Tmr;
    background();
    statusBar(page == 0 ? "STOPWATCH" : "COUNTDOWN", T.yel);
    for (int i = 0; i < 2; i++) cv.fillRect(110 + i * 10, 13, 8, 1, i == page ? T.yel : T.dim);
    if (page == 0) {
      String t = fmt(swNow(), true);
      textC(120, 30, t.c_str(), swRun ? T.white : T.grey, 3);
      for (size_t i = 0; i < laps.size(); i++) {
        char b[32]; snprintf(b, sizeof(b), "LAP %u  %s", (unsigned)(laps.size() - i), fmt(laps[i], true).c_str());
        textC(120, 62 + i * 11, b, i == 0 ? T.cyan : T.dim, 1, false);
      }
      hintBar({{"SPC", swRun ? "STOP" : "START"}, {"L", "LAP"}, {"DEL", "RESET"}, {"TAB", "COUNTDOWN"}});
    } else {
      int32_t left = std::max<int32_t>(0, cdLeft());
      bool flash = ringing && (millis() / 250) % 2;
      if (flash) cv.fillRect(0, 12, 240, 113, lerpc(T.bg0, T.red, 0.4f));
      String t = fmt(left + 999, false);
      textC(120, 30, t.c_str(), ringing ? T.red : cdRun ? T.white : T.grey, 4);
      float f = cdSet ? left / (float)cdSet : 0;
      cv.fillRect(20, 76, 200, 5, T.dim);
      for (int x = 0; x < (int)(200 * f); x++) cv.drawFastVLine(20 + x, 76, 5, gradc(x / 200.f));
      textC(120, 90, ringing ? "TIME'S UP!  PRESS ANY KEY" : "1-5 MIN  6=15 7=25 8=45 9=90", ringing ? T.red : T.dim, 1, false);
      hintBar({{"SPC", cdRun ? "PAUSE" : "START"}, {"^v", "MIN"}, {"<>", "10S"}, {"TAB", "STOPWATCH"}});
    }
  }
};
App* appTimer = new TimerApp();

// ===================================================================== FLASHLIGHT (white / warm / red, strobe, SOS)
struct FlashApp : App {
  int mode = 0;  // 0 white, 1 warm, 2 red (night vision), 3 strobe, 4 SOS
  uint8_t prevBright = 160;
  FlashApp() { name = "FLASHLIGHT"; }
  void enter() override { prevBright = cfg.bright; M5Cardputer.Display.setBrightness(255); }
  void leave() override { M5Cardputer.Display.setBrightness(prevBright); }
  bool key(const KeyEv& e) override {
    KeyCode k = e.nav();
    if (k == K_LEFT || k == K_UP) { mode = (mode + 4) % 5; return true; }
    if (k == K_RIGHT || k == K_DOWN || k == K_ENTER || (e.code == K_CHAR && e.ch == ' ')) { mode = (mode + 1) % 5; return true; }
    return false;
  }
  void draw() override {
    uint32_t t = millis();
    bool on = true;
    if (mode == 3) on = (t / 60) % 2;
    if (mode == 4) {  // ... --- ...
      static const char* pat = "1010100011101110111000101010000000";
      on = pat[(t / 180) % 34] == '1';
    }
    uint16_t c = mode == 1 ? RGB(255, 200, 140) : mode == 2 ? RGB(255, 0, 0) : RGB(255, 255, 255);
    cv.fillSprite(on ? c : RGB(0, 0, 0));
    const char* names[5] = {"WHITE", "WARM", "RED", "STROBE", "SOS"};
    textC(120, 124, names[mode], on ? RGB(120, 120, 120) : RGB(60, 60, 60), 1, false);
  }
};
App* appFlash = new FlashApp();

// ===================================================================== MORSE: type text, hear + see it; decoder trainer
struct MorseApp : App {
  LineEdit in;
  String code;       // dots/dashes of the message being played
  int pos = -1;      // playback position in `code`
  uint32_t next = 0;
  bool lit = false;
  int wpm = 15;
  MorseApp() { name = "MORSE"; in.set("SOS"); }
  bool wantsAllKeys() override { return true; }
  static const char* sym(char c) {
    static const char* A[26] = {".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--.."};
    static const char* D[10] = {"-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----."};
    c = toupper(c);
    if (c >= 'A' && c <= 'Z') return A[c - 'A'];
    if (c >= '0' && c <= '9') return D[c - '0'];
    return "";
  }
  String encode(const String& s) {
    String r;
    for (unsigned i = 0; i < s.length(); i++) {
      if (s[i] == ' ') { r += '/'; continue; }
      const char* m = sym(s[i]);
      if (*m) { r += m; r += ' '; }
    }
    return r;
  }
  bool key(const KeyEv& e) override {
    if (e.code == K_ENTER) { code = encode(in.s); pos = 0; next = millis(); return true; }
    if (e.code == K_TAB) { wpm = wpm >= 25 ? 8 : wpm + 4; return true; }
    if (e.code == K_ESC) { if (pos >= 0) { pos = -1; return true; } return false; }
    return in.handle(e);
  }
  void tick() override {
    if (pos < 0 || millis() < next) return;
    int unit = 1200 / wpm;
    if (lit) { lit = false; next = millis() + unit; return; }  // gap after each element
    if (pos >= (int)code.length()) { pos = -1; return; }
    char c = code[pos++];
    if (c == '.' || c == '-') {
      int d = c == '.' ? unit : unit * 3;
      Audio::blip(700, d, Audio::W_SINE, 0.4f);
      lit = true; next = millis() + d;
    } else next = millis() + unit * (c == '/' ? 4 : 2);
  }
  void draw() override {
    background(true);
    statusBar("MORSE", T.yel);
    if (lit) cv.fillRect(0, 12, 240, 113, lerpc(T.bg0, T.yel, 0.25f));
    panel(4, 16, 232, 16, T.yel, T.panel);
    in.draw(9, 20, 222, T.white, true);
    String c = encode(in.s);
    // show the code, highlighting what is playing
    int x = 8, y = 40;
    for (unsigned i = 0; i < c.length() && y < 100; i++) {
      char ch = c[i];
      uint16_t col = (pos >= 0 && (int)i == pos - 1) ? T.yel : T.cyan;
      if (ch == '.') { cv.fillCircle(x + 2, y + 3, 2, col); x += 7; }
      else if (ch == '-') { cv.fillRect(x, y + 2, 9, 3, col); x += 13; }
      else { x += ch == '/' ? 12 : 6; }
      if (x > 228) { x = 8; y += 12; }
    }
    char b[24]; snprintf(b, sizeof(b), "%d WPM", wpm);
    text(190, 104, b, T.dim, 1, false);
    text(8, 104, pos >= 0 ? "PLAYING..." : "", T.yel, 1, false);
    hintBar({{"TYPE", "TEXT"}, {"OK", "PLAY"}, {"TAB", "SPEED"}, {"ESC", "STOP"}});
  }
};
App* appMorse = new MorseApp();
