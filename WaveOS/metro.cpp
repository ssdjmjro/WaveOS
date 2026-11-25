// WAVE OS - METRONOME (in TOOLS): tap tempo, beats per bar, big flashing beat lights
#include "common.h"
#include "audio.h"

struct MetroApp : App {
  int bpm = 100, beats = 4;
  bool running = false;
  uint32_t taps[4] = {0};
  int ntaps = 0;
  MetroApp() { name = "METRONOME"; }
  void leave() override { running = false; Audio::metronome(0, beats); }
  void restart() { if (running) Audio::metronome(bpm, beats); }
  bool key(const KeyEv& e) override {
    KeyCode k = e.nav();
    if (k == K_UP || k == K_RIGHT) { bpm = std::min(260, bpm + (e.rep ? 5 : 1)); restart(); return true; }
    if (k == K_DOWN || k == K_LEFT) { bpm = std::max(30, bpm - (e.rep ? 5 : 1)); restart(); return true; }
    if (e.code == K_CHAR && e.ch == ' ') { running = !running; Audio::metronome(running ? bpm : 0, beats); return true; }
    if (e.is('b')) { beats = beats % 7 + 1; restart(); return true; }
    if (e.is('t') || k == K_ENTER) {  // tap tempo
      uint32_t now = millis();
      if (ntaps && now - taps[(ntaps - 1) % 4] > 2000) ntaps = 0;
      taps[ntaps % 4] = now; ntaps++;
      if (ntaps >= 2) {
        int n = std::min(ntaps, 4);
        uint32_t first = taps[(ntaps - n) % 4];
        bpm = constrain((int)roundf(60000.f * (n - 1) / (now - first)), 30, 260);
        restart();
      }
      Audio::blip(2000, 15, Audio::W_SINE, 0.3f);
      return true;
    }
    return false;
  }
  void draw() override {
    background();
    statusBar("METRONOME", T.yel);
    char b[16];
    int beat = Audio::metroBeatNow();
    bool flash = running && millis() - Audio::metroFlashT() < 110;
    int cur = running ? (beat + beats - 1) % beats : -1;
    // pendulum
    float ph = running ? (millis() - Audio::metroFlashT()) / (60000.f / bpm) : 0.5f;
    float sw = sinf(PI * constrain(ph, 0.f, 1.f)) * ((beat & 1) ? 1 : -1);
    int px = 196 + (int)(sw * 26);
    cv.drawWideLine(196, 22, px, 80, 1.0f, T.grey);
    cv.fillSmoothCircle(px, 80, 6, flash ? T.white : T.yel);
    cv.fillSmoothCircle(196, 22, 3, T.edge);
    snprintf(b, sizeof(b), "%d", bpm);
    textC(100, 22, b, flash ? T.white : T.yel, 5);
    textC(100, 62, "BPM", T.grey);
    const char* tempo = bpm < 60 ? "LARGO" : bpm < 76 ? "ADAGIO" : bpm < 108 ? "ANDANTE" : bpm < 120 ? "MODERATO" : bpm < 168 ? "ALLEGRO" : "PRESTO";
    textC(100, 74, tempo, T.dim, 1, false);
    int w = beats * 26;
    for (int i = 0; i < beats; i++) {
      int x = 120 - w / 2 + i * 26;
      bool on = i == cur;
      uint16_t c = i == 0 ? T.yel : T.cyan;
      if (on && flash) { cv.fillRect(x, 92, 22, 22, c); frame(x - 1, 91, 24, 24, T.white); }
      else panel(x, 92, 22, 22, on ? c : T.edge, on ? lerpc(T.panel, c, 0.4f) : T.panel);
    }
    snprintf(b, sizeof(b), "%d/4", beats);
    text(6, 16, b, T.grey, 1, false);
    text(6, 26, running ? "RUNNING" : "STOPPED", running ? T.green : T.dim, 1, false);
    hintBar({{"SPC", "START"}, {"^v", "BPM"}, {"T", "TAP"}, {"B", "BEATS"}, {"ESC", "BACK"}});
  }
};

App* appMetro = new MetroApp();
