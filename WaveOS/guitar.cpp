// WAVE OS - guitar tools: chromatic tuner (mic), metronome with tap tempo, chord charts
#include "common.h"
#include "audio.h"

static const char* NOTE[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

struct GuitarApp : App {
  enum { TUNER, METRO, CHORDS, NPAGE };
  int page = TUNER;
  bool micOn = false;
  // tuner
  static constexpr int MSR = 16000, MN = 2048;
  int16_t* mbuf = nullptr;
  float freq = 0, cents = 0, smoothCents = 0, level = 0;
  int note = -1;
  int a4 = 440;
  uint32_t lastHeard = 0;
  // metronome
  int bpm = 100, beats = 4;
  bool running = false;
  uint32_t taps[4] = {0};
  int ntaps = 0;
  // chords
  struct Chord { const char* n; int8_t f[6]; };
  static constexpr int NC = 20;
  const Chord chords[NC] = {
      {"C", {-1, 3, 2, 0, 1, 0}}, {"D", {-1, -1, 0, 2, 3, 2}}, {"E", {0, 2, 2, 1, 0, 0}}, {"F", {1, 3, 3, 2, 1, 1}},
      {"G", {3, 2, 0, 0, 0, 3}}, {"A", {-1, 0, 2, 2, 2, 0}}, {"AM", {-1, 0, 2, 2, 1, 0}}, {"DM", {-1, -1, 0, 2, 3, 1}},
      {"EM", {0, 2, 2, 0, 0, 0}}, {"BM", {-1, 2, 4, 4, 3, 2}}, {"E7", {0, 2, 0, 1, 0, 0}}, {"A7", {-1, 0, 2, 0, 2, 0}},
      {"D7", {-1, -1, 0, 2, 1, 2}}, {"G7", {3, 2, 0, 0, 0, 1}}, {"B7", {-1, 2, 1, 2, 0, 2}}, {"C7", {-1, 3, 2, 3, 1, 0}},
      {"FMAJ7", {-1, -1, 3, 2, 1, 0}}, {"CADD9", {-1, 3, 2, 0, 3, 3}}, {"ASUS2", {-1, 0, 2, 2, 0, 0}}, {"DSUS4", {-1, -1, 0, 2, 3, 3}}};
  int chord = 0;
  int strum = -1;
  uint32_t strumT = 0;
  const uint8_t open[6] = {40, 45, 50, 55, 59, 64};

  GuitarApp() { name = "GUITAR"; }

  void micStart() {
    if (micOn) return;
    if (!mbuf) mbuf = (int16_t*)malloc(MN * 2);
    if (!mbuf) return;
    Audio::suspend(true);
    auto mc = M5.Mic.config();
    mc.sample_rate = MSR;
    M5.Mic.config(mc);
    micOn = M5.Mic.begin();
    if (micOn) M5.Mic.record(mbuf, MN, MSR);
    else { Audio::suspend(false); toast("MIC NOT AVAILABLE"); }
  }
  void micStop() {
    if (!micOn) return;
    while (M5.Mic.isRecording()) delay(1);
    M5.Mic.end();
    micOn = false;
    Audio::suspend(false);
  }
  void enter() override { if (page == TUNER) micStart(); }
  void leave() override {
    micStop();
    if (mbuf) { free(mbuf); mbuf = nullptr; }
  }
  void setPage(int p) {
    if (page == TUNER) micStop();
    page = p;
    if (page == TUNER) micStart();
  }

  // YIN pitch detection
  void analyse() {
    const int W = 1024, TMIN = MSR / 1000, TMAX = MSR / 60;
    static float d[TMAX + 2];
    float mean = 0;
    for (int i = 0; i < MN; i++) mean += mbuf[i];
    mean /= MN;
    float e = 0;
    for (int i = 0; i < MN; i++) e += (mbuf[i] - mean) * (mbuf[i] - mean);
    level = sqrtf(e / MN) / 32768.f;
    if (level < 0.004f) { if (millis() - lastHeard > 1200) note = -1; return; }
    d[0] = 1;
    float run = 0;
    int best = -1;
    for (int tau = 1; tau <= TMAX; tau++) {
      float s = 0;
      for (int i = 0; i < W; i += 1) { float x = mbuf[i] - mbuf[i + tau]; s += x * x; }
      run += s;
      d[tau] = run > 0 ? s * tau / run : 1;
      if (tau > TMIN && best < 0 && d[tau - 1] < 0.15f && d[tau - 1] < d[tau]) best = tau - 1;
    }
    if (best < 0) return;
    float a = d[best - 1], b = d[best], c = d[best + 1];
    float shift = (a - c) / (2 * (a - 2 * b + c));
    float f = MSR / (best + (fabsf(shift) < 1 ? shift : 0));
    if (f < 60 || f > 1000) return;
    freq = f;
    float n = 12 * log2f(f / a4) + 69;
    note = (int)roundf(n);
    cents = (n - note) * 100;
    lastHeard = millis();
  }

  bool key(const KeyEv& e) override {
    if (e.code == K_TAB) { setPage((page + 1) % NPAGE); return true; }
    KeyCode k = e.nav();
    if (page == TUNER) {
      if (e.is('-') || e.is('_')) { a4 = std::max(415, a4 - 1); return true; }
      if (e.code == K_CHAR && (e.ch == '=' || e.ch == '+')) { a4 = std::min(466, a4 + 1); return true; }
      return false;
    }
    if (page == METRO) {
      if (k == K_UP || k == K_RIGHT) { bpm = std::min(260, bpm + (e.rep ? 5 : 1)); if (running) Audio::metronome(bpm, beats); return true; }
      if (k == K_DOWN || k == K_LEFT) { bpm = std::max(30, bpm - (e.rep ? 5 : 1)); if (running) Audio::metronome(bpm, beats); return true; }
      if (e.code == K_CHAR && e.ch == ' ') { running = !running; Audio::metronome(running ? bpm : 0, beats); return true; }
      if (e.is('b')) { beats = beats % 7 + 1; if (running) Audio::metronome(bpm, beats); return true; }
      if (e.is('t') || k == K_ENTER) {  // tap tempo
        uint32_t now = millis();
        if (ntaps && now - taps[(ntaps - 1) % 4] > 2000) ntaps = 0;
        taps[ntaps % 4] = now; ntaps++;
        if (ntaps >= 2) {
          int n = std::min(ntaps, 4);
          uint32_t first = taps[(ntaps - n) % 4];
          bpm = constrain((int)roundf(60000.f * (n - 1) / (now - first)), 30, 260);
          if (running) Audio::metronome(bpm, beats);
        }
        Audio::blip(2000, 15, Audio::W_SINE, 0.3f);
        return true;
      }
      return false;
    }
    // chords
    if (k == K_LEFT) { chord = (chord + NC - 1) % NC; return true; }
    if (k == K_RIGHT) { chord = (chord + 1) % NC; return true; }
    if (k == K_UP) { chord = (chord + NC - 5) % NC; return true; }
    if (k == K_DOWN) { chord = (chord + 5) % NC; return true; }
    if (k == K_ENTER || (e.code == K_CHAR && e.ch == ' ')) { strum = 0; strumT = millis(); return true; }
    return false;
  }

  void tick() override {
    if (page == TUNER && micOn && !M5.Mic.isRecording()) {
      analyse();
      M5.Mic.record(mbuf, MN, MSR);
    }
    smoothCents += (cents - smoothCents) * 0.3f;
    if (strum >= 0 && millis() - strumT >= (uint32_t)strum * 35) {
      const Chord& c = chords[chord];
      if (strum < 6) {
        if (c.f[strum] >= 0) Audio::noteOn(open[strum] + c.f[strum], Audio::W_TRI, 6);
        strum++;
      } else if (millis() - strumT > 1600) {
        for (int s = 0; s < 6; s++) if (c.f[s] >= 0) Audio::noteOff(open[s] + c.f[s], 6);
        strum = -1;
      }
    }
  }

  void drawTuner() {
    bool has = note >= 0;
    uint16_t c = !has ? T.dim : (fabsf(smoothCents) < 5 ? T.green : (fabsf(smoothCents) < 15 ? T.yel : T.red));
    char b[24];
    if (has) {
      textC(120, 22, NOTE[note % 12], c, 5);
      snprintf(b, sizeof(b), "%d", note / 12 - 1);
      text(120 + textW(NOTE[note % 12], 5) / 2 + 4, 46, b, T.grey, 2);
    } else textC(120, 30, "PLAY A STRING", T.grey, 2);
    // cents meter
    int mx = 20, mw = 200, my = 70;
    for (int i = -50; i <= 50; i += 10) {
      int x = mx + (i + 50) * mw / 100;
      cv.drawFastVLine(x, my, i == 0 ? 10 : 5, i == 0 ? T.white : T.dim);
    }
    cv.fillRect(mx + mw / 2 - 10, my + 12, 21, 2, T.green);
    if (has) {
      int x = mx + (int)((constrain(smoothCents, -50.f, 50.f) + 50) * mw / 100);
      cv.fillTriangle(x - 4, my - 6, x + 4, my - 6, x, my, c);
      cv.drawFastVLine(x, my, 14, c);
      snprintf(b, sizeof(b), "%+d CENTS  %.1fHZ", (int)roundf(smoothCents), freq);
      textC(120, 90, b, T.grey, 1, false);
    }
    // strings E A D G B E
    const char* sn[6] = {"E", "A", "D", "G", "B", "E"};
    for (int s = 0; s < 6; s++) {
      int x = 30 + s * 36;
      bool hit = has && note == open[s];
      panel(x - 8, 101, 18, 16, hit ? T.green : T.edge, hit ? lerpc(T.panel, T.green, 0.3f) : T.panel);
      textC(x + 1, 105, sn[s], hit ? T.white : T.grey);
    }
    // level bar
    int lv = std::min(60, (int)(level * 600));
    for (int i = 0; i < lv; i += 2) cv.drawFastVLine(170 + i, 22, 4, gradc(i / 60.f));
    snprintf(b, sizeof(b), "A4=%d", a4);
    text(6, 18, b, T.dim, 1, false);
    hintBar({{"TAB", "PAGE"}, {"-=", "A4 REF"}, {"ESC", "BACK"}});
  }
  void drawMetro() {
    char b[16];
    snprintf(b, sizeof(b), "%d", bpm);
    textC(120, 24, b, T.white, 5);
    textC(120, 62, "BPM", T.grey);
    int beat = Audio::metroBeatNow();
    bool flash = running && millis() - Audio::metroFlashT() < 120;
    int cur = running ? (beat + beats - 1) % beats : -1;
    int w = beats * 22;
    for (int i = 0; i < beats; i++) {
      int x = 120 - w / 2 + i * 22;
      bool on = i == cur;
      uint16_t c = i == 0 ? T.cyan : T.purp;
      if (on && flash) { cv.fillRect(x, 80, 18, 18, c); frame(x - 1, 79, 20, 20, T.white); }
      else panel(x, 80, 18, 18, on ? c : T.edge, on ? lerpc(T.panel, c, 0.4f) : T.panel);
    }
    const char* tempo = bpm < 60 ? "LARGO" : bpm < 76 ? "ADAGIO" : bpm < 108 ? "ANDANTE" : bpm < 120 ? "MODERATO" : bpm < 168 ? "ALLEGRO" : "PRESTO";
    textC(120, 106, tempo, T.dim, 1, false);
    snprintf(b, sizeof(b), "%d/4", beats);
    text(6, 18, b, T.grey, 1, false);
    text(196, 18, running ? "ON" : "OFF", running ? T.green : T.dim, 1, false);
    hintBar({{"SPC", "START"}, {"^v", "BPM"}, {"T", "TAP"}, {"B", "BEATS"}, {"TAB", "PAGE"}});
  }
  void drawChords() {
    const Chord& c = chords[chord];
    textC(60, 20, c.n, T.white, 3);
    // fretboard
    int fx = 120, fy = 26, sw = 20, fh = 17;
    int minF = 99, maxF = 0;
    for (int s = 0; s < 6; s++) if (c.f[s] > 0) { minF = std::min(minF, (int)c.f[s]); maxF = std::max(maxF, (int)c.f[s]); }
    int base = maxF > 4 ? minF : 1;
    cv.fillRect(fx, fy, sw * 5 + 1, base == 1 ? 3 : 1, T.white);
    for (int f = 0; f <= 4; f++) cv.drawFastHLine(fx, fy + f * fh, sw * 5 + 1, T.grey);
    for (int s = 0; s < 6; s++) cv.drawFastVLine(fx + s * sw, fy, fh * 4, T.white);
    if (base > 1) { char b[6]; snprintf(b, sizeof(b), "%dFR", base); text(fx + sw * 5 + 6, fy + 5, b, T.grey, 1, false); }
    for (int s = 0; s < 6; s++) {
      int x = fx + s * sw;
      if (c.f[s] < 0) text(x - 2, fy - 10, "X", T.red, 1, false);
      else if (c.f[s] == 0) cv.drawCircle(x, fy - 6, 3, T.green);
      else {
        int y = fy + (c.f[s] - base) * fh + fh / 2;
        cv.fillCircle(x, y, 5, gradc(s / 5.f));
        cv.drawPixel(x - 1, y - 2, T.white);
      }
    }
    // chord list
    for (int i = 0; i < NC; i++) {
      int x = 6 + (i % 5) * 21, y = 52 + (i / 5) * 17;
      bool s = i == chord;
      if (s) panel(x - 2, y - 3, 21, 13, T.cyan, lerpc(T.panel, T.cyan, 0.2f));
      textFit(x, y, chords[i].n, s ? T.white : T.grey, 20, false);
    }
    hintBar({{"<>^v", "CHORD"}, {"OK", "STRUM"}, {"TAB", "PAGE"}, {"ESC", "BACK"}});
  }
  void draw() override {
    background();
    const char* t[NPAGE] = {"TUNER", "METRONOME", "CHORDS"};
    statusBar(t[page], T.green);
    for (int i = 0; i < NPAGE; i++) cv.fillRect(100 + i * 10, 13, 8, 1, i == page ? T.green : T.dim);
    if (page == TUNER) drawTuner();
    else if (page == METRO) drawMetro();
    else drawChords();
  }
};

App* appGuitar = new GuitarApp();
