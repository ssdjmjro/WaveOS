// WAVE OS - synthesizer + 4 track looper
#include "common.h"
#include "audio.h"
#include <algorithm>

using namespace Audio;

struct SynthApp : App {
  // white keys: a s d f g h j k l ; '   black keys: w e t y u o p
  static int semitone(char c) {
    switch (c) {
      case 'a': return 0;  case 'w': return 1;  case 's': return 2;  case 'e': return 3;  case 'd': return 4;
      case 'f': return 5;  case 't': return 6;  case 'g': return 7;  case 'y': return 8;  case 'h': return 9;
      case 'u': return 10; case 'j': return 11; case 'k': return 12; case 'o': return 13; case 'l': return 14;
      case 'p': return 15; case ';': return 16; case '\'': return 17;
    }
    return -1;
  }
  struct Held { char c; std::vector<uint8_t> notes; };
  std::vector<Held> held;
  std::vector<uint8_t> sustained;  // notes kept by HOLD
  int octave = 4;
  int transpose = 0;   // semitones
  int chord = 0;       // 0 off, 1 maj, 2 min, 3 7th, 4 power, 5 sus4
  int layer = 0;       // 0 off, 1 +oct, 2 -oct, 3 +5th, 4 +/-oct
  bool hold = false;
  const char* CHORD[6] = {"", "MAJ", "MIN", "7TH", "PWR", "SUS4"};
  const char* LAYER[5] = {"", "+OCT", "-OCT", "+5TH", "2OCT"};
  const char* ARP[5] = {"", "UP", "DOWN", "UPDN", "RND"};
  int track = 0;
  int param = 4;
  bool help = false;
  struct Param { const char* n; float* v; float lo, hi; bool time; };
  Param params[9] = {
      {"ATTACK", &sp.a, 0.002f, 2.f, true}, {"DECAY", &sp.d, 0.01f, 2.f, true}, {"SUSTAIN", &sp.s, 0, 1, false},
      {"RELEASE", &sp.r, 0.01f, 3.f, true}, {"CUTOFF", &sp.cutoff, 0, 1, false}, {"RESO", &sp.reso, 0, 0.95f, false},
      {"DELAY", &sp.delayMix, 0, 0.8f, false}, {"FEEDBACK", &sp.delayFb, 0, 0.85f, false}, {"VOLUME", &sp.vol, 0, 1, false}};
  static constexpr int NP = 9;

  SynthApp() { name = "SYNTH"; }
  bool wantsAllKeys() override { return true; }
  void releaseNote(uint8_t n) {
    if (arpMode) arpNote(n, false, 0); else noteOff(n, 0);
    Looper::recNote(n, false);
  }
  void releaseAll() {
    for (auto& h : held) for (auto n : h.notes) releaseNote(n);
    for (auto n : sustained) releaseNote(n);
    held.clear(); sustained.clear();
  }
  void leave() override { releaseAll(); }
  std::vector<uint8_t> notesFor(int base) {
    static const int8_t CH[6][4] = {{0, -1, -1, -1}, {0, 4, 7, -1}, {0, 3, 7, -1}, {0, 4, 7, 10}, {0, 7, 12, -1}, {0, 5, 7, -1}};
    std::vector<uint8_t> v;
    auto add = [&](int n) { if (n >= 0 && n < 128 && std::find(v.begin(), v.end(), n) == v.end()) v.push_back(n); };
    for (int i = 0; i < 4 && CH[chord][i] >= 0; i++) {
      int n = base + CH[chord][i];
      add(n);
      if (layer == 1 || layer == 4) add(n + 12);
      if (layer == 2 || layer == 4) add(n - 12);
      if (layer == 3) add(n + 7);
    }
    return v;
  }

  void adjust(int d) {
    Param& p = params[param];
    if (p.time) *p.v = constrain(*p.v * (d > 0 ? 1.3f : 1 / 1.3f), p.lo, p.hi);
    else *p.v = constrain(*p.v + d * 0.05f, p.lo, p.hi);
  }

  bool key(const KeyEv& e) override {
    if (help) { help = false; return true; }
    if (e.code == K_CHAR && !e.fn && semitone(tolower(e.ch)) >= 0) return true;  // notes handled in tick()
    if (e.code == K_BACK) {
      if (e.fn || e.shift) { Looper::clear(track); toast("TRACK CLEARED"); }
      else { Looper::undo(track); toast("UNDO LAST TAKE"); }
      return true;
    }
    if (e.code == K_TAB) { param = (param + 1) % NP; return true; }
    if (e.code != K_CHAR) return false;
    char c = tolower(e.ch);
    if (e.fn) {
      if (c == 's') { ensureDir("/loops"); toast(Looper::save("/loops/jam.wlp") ? "SAVED /LOOPS/JAM.WLP" : "SAVE FAILED"); return true; }
      if (c == 'e') { ensureDir("/loops"); toast(Looper::exportMidi("/loops/jam.mid") ? "EXPORTED /LOOPS/JAM.MID" : "EXPORT FAILED"); return true; }
      if (c == 'l') { if (Looper::load("/loops/jam.wlp")) { track = 0; toast("LOOP LOADED"); } else toast("NO SAVED LOOP"); return true; }
      return false;
    }
    char b[32];
    switch (c) {
      case 'z': octave = std::max(0, octave - 1); snprintf(b, sizeof(b), "OCTAVE %d", octave); toast(b, 600); return true;
      case 'x': octave = std::min(8, octave + 1); snprintf(b, sizeof(b), "OCTAVE %d", octave); toast(b, 600); return true;
      case ',': case '<': transpose = std::max(-12, transpose - 1); snprintf(b, sizeof(b), "TRANSPOSE %+d", transpose); toast(b, 600); return true;
      case '/': transpose = std::min(12, transpose + 1); snprintf(b, sizeof(b), "TRANSPOSE %+d", transpose); toast(b, 600); return true;
      case '5': chord = (chord + 1) % 6; toast(chord ? (String("CHORD ") + CHORD[chord]).c_str() : "CHORD OFF", 700); return true;
      case '6': releaseAll(); arpMode = (arpMode + 1) % 5; arpClear(); toast(arpMode ? (String("ARP ") + ARP[arpMode]).c_str() : "ARP OFF", 700); return true;
      case '7': arpOct = arpOct % 3 + 1; snprintf(b, sizeof(b), "ARP RANGE %d OCT", (int)arpOct); toast(b, 700); return true;
      case '8': layer = (layer + 1) % 5; toast(layer ? (String("LAYER ") + LAYER[layer]).c_str() : "LAYER OFF", 700); return true;
      case '9':
        hold = !hold;
        if (!hold) { for (auto n : sustained) releaseNote(n); sustained.clear(); }
        toast(hold ? "HOLD ON" : "HOLD OFF", 700); return true;
      case 'c': Looper::tr[track].wave = (Looper::tr[track].wave + 1) % W_COUNT; return true;
      case '[': param = (param + NP - 1) % NP; return true;
      case ']': param = (param + 1) % NP; return true;
      case '-': case '_': adjust(-1); return true;
      case '=': case '+': adjust(1); return true;
      case 'r': Looper::toggleRec(track); return true;
      case ' ': if (Looper::playing || Looper::counting) Looper::stop(); else Looper::play(); return true;
      case '1': case '2': case '3': case '4':
        if (!Looper::recording) track = c - '1';
        return true;
      case 'm': Looper::tr[track].mute = !Looper::tr[track].mute; allOff(track + 1); return true;
      case 'q': Looper::quant = (Looper::quant + 1) % 3; snprintf(b, sizeof(b), "QUANTIZE %s", Looper::quant == 0 ? "OFF" : (Looper::quant == 1 ? "1/8" : "1/16")); toast(b); return true;
      case 'b': Looper::bpm = std::max(40, Looper::bpm - (e.rep ? 5 : 1)); return true;
      case 'n': Looper::bpm = std::min(240, Looper::bpm + (e.rep ? 5 : 1)); return true;
      case 'v':
        if (Looper::empty() && !Looper::playing) { Looper::bars = Looper::bars >= 8 ? 1 : Looper::bars * 2; }
        else toast("CLEAR LOOPS TO CHANGE BARS");
        return true;
      case '0': Looper::metro = !Looper::metro; toast(Looper::metro ? "METRONOME ON" : "METRONOME OFF"); return true;
      case '\\': case '?': help = true; return true;
    }
    return false;
  }

  void tick() override {
    // note on/off from the held key list (polyphonic)
    std::vector<char> now;
    if (!heldFn)
      for (char k : heldKeys) if (semitone(k) >= 0) now.push_back(k);
    for (int i = held.size() - 1; i >= 0; i--) {
      if (std::find(now.begin(), now.end(), held[i].c) == now.end()) {
        for (auto n : held[i].notes) {
          if (hold) { if (std::find(sustained.begin(), sustained.end(), n) == sustained.end()) sustained.push_back(n); }
          else releaseNote(n);
        }
        held.erase(held.begin() + i);
      }
    }
    for (char k : now) {
      bool was = false;
      for (auto& h : held) if (h.c == k) was = true;
      if (was) continue;
      int base = 12 * (octave + 1) + semitone(k) + transpose;
      Held h{k, notesFor(base)};
      for (auto n : h.notes) {
        auto it = std::find(sustained.begin(), sustained.end(), n);
        if (it != sustained.end()) { sustained.erase(it); releaseNote(n); }
        if (arpMode) arpNote(n, true, Looper::tr[track].wave);
        else noteOn(n, Looper::tr[track].wave, 0);
        Looper::recNote(n, true);
      }
      held.push_back(h);
    }
  }

  void drawADSR(int x, int y, int w, int h) {
    float tot = sp.a + sp.d + 0.4f + sp.r;
    int xa = x + (int)(w * sp.a / tot), xd = xa + (int)(w * sp.d / tot), xs = xd + (int)(w * 0.4f / tot);
    int yb = y + h, ys = y + (int)(h * (1 - sp.s));
    cv.drawLine(x, yb, xa, y, T.cyan);
    cv.drawLine(xa, y, xd, ys, T.cyan);
    cv.drawLine(xd, ys, xs, ys, T.cyan);
    cv.drawLine(xs, ys, x + w, yb, T.cyan);
    cv.fillRect(xa - 1, y - 1, 3, 3, T.white);
    cv.fillRect(xd - 1, ys - 1, 3, 3, T.white);
    cv.fillRect(xs - 1, ys - 1, 3, 3, T.white);
  }

  void draw() override {
    background(true);
    for (int y = 13; y < 125; y += 4) for (int x = 0; x < 240; x += 4) cv.drawPixel(x, y, T.bg1);
    const char* st = Looper::counting ? "SYNTH  COUNT-IN" : (Looper::recording ? "SYNTH  REC" : (Looper::playing ? "SYNTH  PLAY" : "SYNTH"));
    statusBar(st, Looper::recording || Looper::counting ? T.red : T.purp);

    // --- scope
    panel(3, 14, 74, 38, T.purpD, T.bg0);
    for (int x = 8; x < 76; x += 8) for (int y = 16; y < 51; y += 2) cv.drawPixel(x, y, T.bg2);
    int16_t s[70];
    getScope(s, 70, false);
    int py = 33;
    for (int i = 0; i < 70; i++) {
      int y = constrain(33 - s[i] * 26 / 32768, 15, 50);
      if (i) cv.drawLine(4 + i, py, 5 + i, y, T.purp);
      py = y;
    }
    {  // performance modes overlay (scope panel)
      char m1[24], m2[24];
      snprintf(m1, sizeof(m1), "%s%s%s", chord ? CHORD[chord] : "", chord && layer ? " " : "", layer ? LAYER[layer] : "");
      snprintf(m2, sizeof(m2), "%s%s%s", arpMode ? "ARP " : "", arpMode ? ARP[arpMode] : "", hold ? " HOLD" : "");
      if (*m1) text(6, 34, m1, T.yel, 1, false);
      if (*m2) text(6, 43, m2, T.green, 1, false);
      if (transpose) { char t[8]; snprintf(t, sizeof(t), "%+d", transpose); text(56, 17, t, T.cyan, 1, false); }
    }
    // --- info
    panel(80, 14, 78, 38, T.purpD);
    char b[40];
    snprintf(b, sizeof(b), "%s", waveNames[Looper::tr[track].wave]);
    text(84, 17, b, T.white);
    snprintf(b, sizeof(b), "OCT%d", octave);
    text(130, 17, b, T.cyan, 1, false);
    Param& p = params[param];
    text(84, 29, p.n, T.purp, 1, false);
    if (p.time) snprintf(b, sizeof(b), "%dMS", (int)(*p.v * 1000));
    else snprintf(b, sizeof(b), "%d%%", (int)(*p.v / p.hi * 100));
    text(154 - textW(b), 29, b, T.grey, 1, false);
    float fr = p.time ? (logf(*p.v / p.lo) / logf(p.hi / p.lo)) : (*p.v - p.lo) / (p.hi - p.lo);
    cv.fillRect(84, 40, 70, 3, T.dim);
    for (int i = 0; i < (int)(70 * fr); i++) cv.drawFastVLine(84 + i, 40, 3, gradc(i / 70.f));
    for (int i = 0; i < NP; i++) cv.fillRect(84 + i * 8, 46, 6, 2, i == param ? T.purp : T.dim);
    // --- ADSR
    panel(161, 14, 76, 38, T.purpD, T.bg0);
    drawADSR(166, 20, 66, 26);

    // --- looper
    panel(3, 54, 234, 46, T.edge);
    snprintf(b, sizeof(b), "%dBPM %dBAR Q%s %s", Looper::bpm, Looper::bars,
             Looper::quant == 0 ? "-" : (Looper::quant == 1 ? "8" : "16"), Looper::metro ? "MET" : "");
    text(7, 57, b, T.grey, 1, false);
    if (Looper::counting) { snprintf(b, sizeof(b), "IN %d", (int)ceilf(Looper::countBeats())); text(190, 57, b, T.red, 1, false); }
    else if (Looper::recording) { if ((millis() / 300) & 1) cv.fillCircle(200, 60, 3, T.red); text(206, 57, "REC", T.red, 1, false); }
    else if (Looper::playing) text(200, 57, "PLAY", T.green, 1, false);
    uint32_t L = Looper::lenTicks();
    int lx = 46, lw = 186;
    // beat grid
    for (uint32_t bt = 0; bt <= L; bt += Looper::PPQN) {
      int x = lx + (int)((uint64_t)lw * bt / L);
      bool bar = (bt / Looper::PPQN) % 4 == 0;
      for (int y = 67; y < 98; y += bar ? 1 : 3) cv.drawPixel(x, y, bar ? T.edge : T.bg2);
    }
    static std::vector<Looper::Ev> copy;
    for (int t = 0; t < Looper::TRACKS; t++) {
      int y = 67 + t * 8;
      bool selT = t == track;
      uint16_t tc = Looper::tr[t].mute ? T.dim : (t == 0 ? T.cyan : t == 1 ? T.purp : t == 2 ? T.pink : T.green);
      if (selT) cv.fillRect(5, y - 1, 230, 8, lerpc(T.panel, tc, 0.15f));
      snprintf(b, sizeof(b), "%d%s", t + 1, Looper::tr[t].mute ? "M" : "");
      text(7, y, b, selT ? T.white : T.grey, 1, false);
      text(20, y, waveNames[Looper::tr[t].wave], tc, 1, false);
      Looper::lock();
      copy = Looper::tr[t].ev;
      Looper::unlock();
      // draw notes as bars from on to off
      for (size_t i = 0; i < copy.size(); i++) {
        if (!copy[i].on) continue;
        uint32_t t0 = copy[i].t, t1 = t0 + Looper::PPQN / 4;
        for (size_t j = 0; j < copy.size(); j++) {
          size_t k = (i + 1 + j) % copy.size();
          if (!copy[k].on && copy[k].note == copy[i].note) { t1 = copy[k].t; break; }
        }
        int nx0 = lx + (int)((uint64_t)lw * t0 / L);
        int nx1 = lx + (int)((uint64_t)lw * (t1 >= t0 ? t1 : L) / L);
        int ny = y + 5 - (copy[i].note % 12) * 5 / 12;
        cv.drawFastHLine(nx0, ny, std::max(1, nx1 - nx0), tc);
      }
    }
    if (Looper::playing) {
      int x = lx + (int)(lw * Looper::posTicks() / L);
      cv.drawFastVLine(x, 66, 33, T.white);
    }

    // --- piano
    static const char wk[11] = {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\''};
    static const char bk[11] = {'w', 'e', 0, 't', 'y', 'u', 0, 'o', 'p', 0, 0};
    int kx = 3, kw = 21, ky = 102;
    for (int i = 0; i < 11; i++) {
      uint8_t n = 12 * (octave + 1) + semitone(wk[i]);
      bool on = voiceActive(n);
      uint16_t c = on ? lerpc(T.white, T.cyan, 0.6f) : RGB(200, 210, 230);
      if (cfg.mono) c = on ? T.grey : T.white;
      cv.fillRect(kx + i * kw, ky, kw - 1, 22, c);
      cv.fillRect(kx + i * kw, ky + 20, kw - 1, 2, T.grey);
      char l[2] = {(char)toupper(wk[i]), 0};
      text(kx + i * kw + 8, ky + 12, l, on ? T.bg0 : T.grey, 1, false);
    }
    for (int i = 0; i < 11; i++) {
      if (!bk[i]) continue;
      uint8_t n = 12 * (octave + 1) + semitone(bk[i]);
      bool on = voiceActive(n);
      int x = kx + i * kw + 14;
      cv.fillRect(x, ky, 12, 12, on ? T.purp : RGB(14, 18, 30));
      char l[2] = {(char)toupper(bk[i]), 0};
      text(x + 3, ky + 3, l, on ? T.white : T.dim, 1, false);
    }
    hintBar({{"ZX", "OCT"}, {"5-9", "FX"}, {"R", "REC"}, {"SPC", "LOOP"}, {"\\", "HELP"}});
    if (help)
      helpOverlay("SYNTH + LOOPER", {"A-'  WHITE KEYS", "W-P  BLACK KEYS", "Z X  OCTAVE", "C  WAVEFORM", "TAB  NEXT PARAM", "[ ]  PARAM", "- =  ADJUST",
                                     "5  CHORD MODE", "6  ARPEGGIATOR", "7  ARP RANGE", "8  OCTAVE LAYER", "9  HOLD", ", /  TRANSPOSE", "0  METRONOME", "R  REC/OVERDUB", "SPC  PLAY/STOP", "1-4  TRACK", "M  MUTE TRACK", "Q  QUANTIZE", "B N  BPM", "V  BARS",
                                     "DEL  UNDO TAKE", "FN+DEL  CLEAR", "FN+S  SAVE", "FN+L  LOAD", "FN+E  EXPORT MID"});
  }
};

App* appSynth = new SynthApp();
