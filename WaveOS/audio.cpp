// WAVE OS - audio engine: one mixer task feeds M5.Speaker with 3 rotating buffers.
#include "audio.h"
#include <M5Cardputer.h>
#include <SD.h>
#include <math.h>
#include <algorithm>
#include <AudioFileSourceSD.h>
#include <AudioFileSourceID3.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorFLAC.h>
#include <AudioGeneratorAAC.h>

// ====================================================================== shared state
namespace Audio {
const char* waveNames[W_COUNT] = {"SINE", "SQUARE", "SAW", "TRI", "NOISE"};
SynthParams sp;

static int16_t outBuf[3][BLOCK];
static float mix[BLOCK], syn[BLOCK];
static float sineTab[257];

constexpr uint8_t OWNER_SFX = 9;
struct Voice {
  bool on = false;
  uint8_t note = 0, wave = 0, owner = 0, stage = 0;
  float phase = 0, inc = 0, env = 0, vel = 1;
  uint32_t age = 0, rng = 22222;
  int32_t sfxLeft = -1;
};
constexpr int NV = 16;
static Voice V[NV];
static uint32_t ageCtr = 0;

struct Cmd { uint8_t type, note, wave, owner; float freq, vol; int ms; };
static QueueHandle_t cmdQ;

static int16_t visMus[VIS], visSyn[VIS];
static int visMusI = 0, visSynI = 0;

// delay line (synth bus)
constexpr int DL = 6400;  // max 200ms echo (saves RAM)
static int16_t* dline = nullptr;
static int dIdx = 0;

// metronome click
static int clickLeft = 0;
static float clickPh = 0, clickInc = 0;

static float noteFreq(uint8_t n) { return 440.f * powf(2.f, (n - 69) / 12.f); }

static void vOn(uint8_t note, uint8_t wave, uint8_t owner, float freq = 0, int ms = -1, float vel = 1) {
  int idx = -1;
  if (ms < 0)
    for (int i = 0; i < NV; i++)
      if (V[i].on && V[i].note == note && V[i].owner == owner) { idx = i; break; }
  if (idx < 0)
    for (int i = 0; i < NV; i++)
      if (!V[i].on) { idx = i; break; }
  if (idx < 0) {  // steal: prefer released voices, then the oldest
    uint32_t best = UINT32_MAX;
    for (int i = 0; i < NV; i++) {
      uint32_t score = V[i].age - (V[i].stage == 4 ? 1000000u : 0u);
      if (score < best) { best = score; idx = i; }
    }
  }
  Voice& v = V[idx];
  bool retrig = v.on && v.note == note && v.owner == owner;
  v.on = true; v.note = note; v.wave = wave; v.owner = owner; v.stage = 1;
  if (!retrig) { v.env = 0; v.phase = 0; }
  v.inc = (freq > 0 ? freq : noteFreq(note)) / SR;
  v.age = ++ageCtr;
  v.sfxLeft = ms > 0 ? (int32_t)ms * SR / 1000 : -1;
  v.vel = vel;
}
static void vOff(uint8_t note, uint8_t owner) {
  for (auto& v : V)
    if (v.on && v.note == note && v.owner == owner && v.stage < 4) v.stage = 4;
}
static void vAllOff(int owner) {
  for (auto& v : V)
    if (v.on && (owner < 0 || v.owner == owner)) v.stage = 4;
}

void noteOn(uint8_t note, uint8_t wave, uint8_t owner) { Cmd c{0, note, wave, owner, 0, 1, -1}; xQueueSend(cmdQ, &c, 0); }
void noteOff(uint8_t note, uint8_t owner) { Cmd c{1, note, 0, owner, 0, 1, -1}; xQueueSend(cmdQ, &c, 0); }
void allOff(int owner) { Cmd c{2, 0, 0, (uint8_t)(owner < 0 ? 255 : owner), 0, 1, -1}; xQueueSend(cmdQ, &c, 0); }
void blip(float freq, int ms, uint8_t wave, float vol) { Cmd c{3, 0, wave, OWNER_SFX, freq, vol, ms}; xQueueSend(cmdQ, &c, 0); }
bool voiceActive(uint8_t note) {
  for (auto& v : V)
    if (v.on && v.stage < 4 && v.note == note && v.owner != OWNER_SFX) return true;
  return false;
}

static void arpAdd(uint8_t n, uint8_t w);
static void arpRemove(uint8_t n);
static void arpReset();
static void dTrig(int type, float vel);
static void processCmds() {
  Cmd c;
  while (xQueueReceive(cmdQ, &c, 0) == pdTRUE) {
    switch (c.type) {
      case 0: vOn(c.note, c.wave, c.owner); break;
      case 1: vOff(c.note, c.owner); break;
      case 2: vAllOff(c.owner == 255 ? -1 : c.owner); break;
      case 3: vOn(0, c.wave, OWNER_SFX, c.freq, c.ms, c.vol); break;
      case 4: arpAdd(c.note, c.wave); break;
      case 5: arpRemove(c.note); break;
      case 6: dTrig(c.note, c.vol); break;
      case 7: arpReset(); break;
    }
  }
}

static void click(bool down) {
  clickLeft = SR / 30;
  clickPh = 0;
  clickInc = (down ? 1800.f : 1100.f) / SR;
}

void getScope(int16_t* out, int n, bool music) {
  const int16_t* src = music ? visMus : visSyn;
  int start = (music ? visMusI : visSynI) - n;
  for (int i = 0; i < n; i++) out[i] = src[(start + i + VIS * 4) % VIS];
}

static uint8_t lastVol = 140;
static volatile int metroBpm = 0, metroBeats = 4, metroBeat = 0;
static double metroAcc = 0;
static volatile uint32_t metroFlash = 0;
void setVolume(uint8_t v) { lastVol = v; M5.Speaker.setVolume(v); }
void useDelay() { if (!dline) dline = (int16_t*)calloc(DL, sizeof(int16_t)); }  // synth echo buffer: only when the synth is used

// ---------------------------------------------------------------- arpeggiator (owner 7)
constexpr uint8_t OWNER_ARP = 7;
volatile int arpMode = 0;  // 0 off, 1 up, 2 down, 3 up-down, 4 random
volatile int arpOct = 1;   // octave range 1..3
static uint8_t arpNotes[16], arpN = 0, arpWave = 2, arpCur = 0;
static int arpIdx = 0, arpDir = 1;
static double arpAcc = 1e9;
static bool arpPlaying = false;
static void arpAdd(uint8_t n, uint8_t w) {
  arpWave = w;
  for (int i = 0; i < arpN; i++) if (arpNotes[i] == n) return;
  if (arpN < 16) {
    int i = arpN++;
    while (i > 0 && arpNotes[i - 1] > n) { arpNotes[i] = arpNotes[i - 1]; i--; }
    arpNotes[i] = n;
  }
  if (arpN == 1) { arpAcc = 1e9; arpIdx = -1; arpDir = 1; }
}
static void arpRemove(uint8_t n) {
  for (int i = 0; i < arpN; i++)
    if (arpNotes[i] == n) { for (int j = i; j < arpN - 1; j++) arpNotes[j] = arpNotes[j + 1]; arpN--; break; }
  if (!arpN && arpPlaying) { vOff(arpCur, OWNER_ARP); arpPlaying = false; }
}
static void arpReset() { arpN = 0; if (arpPlaying) { vOff(arpCur, OWNER_ARP); arpPlaying = false; } }
void arpNote(uint8_t note, bool on, uint8_t wave) { Cmd c{(uint8_t)(on ? 4 : 5), note, wave, OWNER_ARP, 0, 1, -1}; xQueueSend(cmdQ, &c, 0); }
void arpClear() { Cmd c{7, 0, 0, OWNER_ARP, 0, 1, -1}; xQueueSend(cmdQ, &c, 0); }
static void arpChunk(int n, int bpm) {
  if (!arpMode || !arpN) return;
  double stepLen = SR * 60.0 / bpm / 4;
  arpAcc += n;
  if (arpAcc < stepLen) return;
  arpAcc = 0;
  int total = arpN * arpOct;
  if (arpMode == 4) arpIdx = random(total);
  else if (arpMode == 1) arpIdx = (arpIdx + 1) % total;
  else if (arpMode == 2) arpIdx = arpIdx <= 0 ? total - 1 : arpIdx - 1;
  else {
    if (total == 1) arpIdx = 0;
    else { arpIdx += arpDir; if (arpIdx >= total) { arpDir = -1; arpIdx = total - 2; } if (arpIdx < 0) { arpDir = 1; arpIdx = 1; } }
  }
  arpIdx = constrain(arpIdx, 0, total - 1);
  uint8_t note = arpNotes[arpIdx % arpN] + 12 * (arpIdx / arpN);
  if (arpPlaying) vOff(arpCur, OWNER_ARP);
  vOn(note, arpWave, OWNER_ARP);
  arpCur = note; arpPlaying = true;
}

// ---------------------------------------------------------------- drum synth
struct DV { bool on = false; uint8_t type = 0; uint32_t n = 0; float ph = 0, vel = 1, prev = 0; uint32_t rng = 777; };
static DV dv[10];
static void dTrig(int type, float vel) {
  int idx = 0; uint32_t best = 0;
  for (int i = 0; i < 10; i++) {
    if (!dv[i].on) { idx = i; break; }
    if (dv[i].n > best) { best = dv[i].n; idx = i; }
  }
  if (type == 2 || type == 3)  // hats choke each other
    for (auto& d : dv) if (d.on && (d.type == 2 || d.type == 3)) d.on = false;
  DV& d = dv[idx];
  d.on = true; d.type = type; d.n = 0; d.ph = 0; d.vel = vel; d.prev = 0;
}
void drumHit(int type, float vel) { Cmd c{6, (uint8_t)type, 0, 0, 0, vel, -1}; xQueueSend(cmdQ, &c, 0); }
float drumVol = 0.8f;
static inline float dnoise(DV& d) { d.rng = d.rng * 1664525u + 1013904223u; return (int16_t)(d.rng >> 16) / 32768.f; }
static void drumRender(float* out, int off, int n) {
  for (auto& d : dv) {
    if (!d.on) continue;
    for (int i = off; i < off + n; i++) {
      float t = d.n / (float)SR, s = 0;
      switch (d.type) {
        case 0: { float f = 45 + 110 * expf(-t / 0.03f); d.ph += f / SR; s = sinf(2 * PI * d.ph) * expf(-t / 0.28f) * 1.1f; if (t > 0.6f) d.on = false; } break;
        case 1: s = dnoise(d) * expf(-t / 0.09f) * 0.55f + sinf(2 * PI * 185 * t) * expf(-t / 0.05f) * 0.45f; if (t > 0.35f) d.on = false; break;
        case 2: case 3: { float x = dnoise(d), y = x - d.prev; d.prev = x; s = y * expf(-t / (d.type == 2 ? 0.025f : 0.2f)) * 0.35f; if (t > (d.type == 2 ? 0.12f : 0.6f)) d.on = false; } break;
        case 4: { float e = 0; for (int k = 0; k < 3; k++) { float tk = t - k * 0.011f; if (tk >= 0) e = std::max(e, expf(-tk / 0.006f)); } e = std::max(e, t > 0.03f ? expf(-(t - 0.03f) / 0.12f) * 0.6f : 0.f); s = dnoise(d) * e * 0.5f; if (t > 0.4f) d.on = false; } break;
        case 5: { float f = 90 + 70 * expf(-t / 0.05f); d.ph += f / SR; s = sinf(2 * PI * d.ph) * expf(-t / 0.22f) * 0.8f; if (t > 0.6f) d.on = false; } break;
        case 6: s = (sinf(2 * PI * 820 * t) * 0.6f + dnoise(d) * 0.3f) * expf(-t / 0.012f); if (t > 0.06f) d.on = false; break;
        default: { float a = fmodf(t * 540, 1) < 0.5f ? 1 : -1, b = fmodf(t * 800, 1) < 0.5f ? 1 : -1; s = (a + b) * 0.18f * expf(-t / 0.12f); if (t > 0.4f) d.on = false; } break;
      }
      out[i] += s * d.vel * drumVol;
      d.n++;
      if (!d.on) break;
    }
  }
}
}  // namespace Audio

namespace Drums {
using namespace Audio;
uint16_t pat[8][4];  // [track][pattern] bitmask of 16 steps
volatile int cur = 0, queued = -1;
bool mute[8];
volatile bool playing = false;
volatile int step = -1;
int swing = 0;  // 0..50 %
static double acc = 0;
void play() {
  if (playing) return;
  step = -1; acc = 1e9;
  if (Looper::playing) {  // lock to the looper's beat
    double sixteenth = Looper::PPQN / 4.0;
    int s = (int)(Looper::posTicks() / sixteenth);
    step = (s - 1 + 16) % 16;
    acc = 0;
  }
  playing = true;
}
void stop() { playing = false; step = -1; }
static void chunk(int n) {
  if (!playing) return;
  double sp = SR * 60.0 / Looper::bpm / 4;
  double len = sp * (1 + ((step + 1) % 2 ? swing : -swing) / 100.0 * 0.5);
  acc += n;
  if (acc < len) return;
  acc = acc >= 1e8 ? 0 : acc - len;
  step = (step + 1) % 16;
  if (step == 0 && queued >= 0) { cur = queued; queued = -1; }
  for (int t = 0; t < 8; t++)
    if (!mute[t] && (pat[t][cur] >> step & 1)) dTrig(t, step % 4 == 0 ? 1.0f : 0.85f);
}
}  // namespace Drums

// ---------------------------------------------------------------- MIDI file player
namespace MidiPlay {
using namespace Audio;
struct E { uint32_t s; uint8_t st, d1, d2; };
static E* ev = nullptr;
static uint32_t nev = 0, idx = 0, pos = 0, lenS = 0;
static uint8_t prog[16];
volatile bool playing = false;
static uint8_t waveFor(uint8_t p) {
  if (p < 8) return W_TRI; if (p < 16) return W_SINE; if (p < 24) return W_SQUARE; if (p < 32) return W_SAW;
  if (p < 40) return W_TRI; if (p < 56) return W_SAW; if (p < 80) return W_SQUARE; return W_SAW;
}
static int drumFor(uint8_t n) {
  switch (n) {
    case 35: case 36: return 0; case 38: case 40: return 1; case 42: case 44: return 2; case 46: case 49: case 51: case 57: return 3;
    case 39: return 4; case 41: case 43: case 45: case 47: case 48: case 50: return 5; case 37: return 6; case 56: return 7;
  }
  return n < 40 ? 0 : 2;
}
static void dispatch(const E& e) {
  uint8_t ch = e.st & 15, ty = e.st & 0xF0;
  if (ty == 0x90 && e.d2) {
    if (ch == 9) dTrig(drumFor(e.d1), e.d2 / 127.f);
    else vOn(e.d1, waveFor(prog[ch]), 10 + ch, 0, -1, 0.4f + 0.6f * e.d2 / 127.f);
  } else if (ty == 0x80 || ty == 0x90) {
    if (ch != 9) vOff(e.d1, 10 + ch);
  } else if (ty == 0xC0) prog[ch] = e.d1;
}
static void chunk(int n) {
  if (!playing || !ev) return;
  uint32_t end = pos + n;
  while (idx < nev && ev[idx].s < end) dispatch(ev[idx++]);
  pos = end;
  if (idx >= nev && pos > lenS + SR) { playing = false; for (int c = 0; c < 16; c++) vAllOff(10 + c); }
}
void stop() {
  playing = false;
  for (int c = 0; c < 16; c++) allOff(10 + c);
}
void play() { if (!ev) return; idx = 0; pos = 0; memset(prog, 0, sizeof(prog)); playing = true; }
void unload() { if (playing) return; if (ev) { free(ev); ev = nullptr; } nev = 0; }
float posSec() { return pos / (float)SR; }
float lenSec() { return lenS / (float)SR; }
uint32_t events() { return nev; }

static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
bool load(const char* path) {
  stop();
  delay(30);
  if (ev) { free(ev); ev = nullptr; nev = 0; }
  File f = SD.open(path);
  if (!f) return false;
  size_t sz = f.size();
  if (sz < 14 || sz > 120000 || sz + 60000 > ESP.getMaxAllocHeap()) { f.close(); return false; }
  uint8_t* b = (uint8_t*)malloc(sz);
  if (!b) { f.close(); return false; }
  f.read(b, sz); f.close();
  bool ok = false;
  if (!memcmp(b, "MThd", 4)) {
    int ntr = b[10] << 8 | b[11], div = b[12] << 8 | b[13];
    if (!(div & 0x8000) && div > 0) {
      struct RE { uint32_t tick; uint8_t st, d1, d2; uint32_t tempo; };
      std::vector<RE> all;
      all.reserve(2000);
      size_t p = 8 + rd32(b + 4);
      for (int t = 0; t < ntr && p + 8 <= sz; t++) {
        if (memcmp(b + p, "MTrk", 4)) break;
        size_t len = rd32(b + p + 4), q = p + 8, end = std::min(sz, q + len);
        uint32_t tick = 0;
        uint8_t run = 0;
        while (q < end && all.size() < 6000) {
          uint32_t dt = 0;
          while (q < end) { uint8_t c = b[q++]; dt = (dt << 7) | (c & 0x7F); if (!(c & 0x80)) break; }
          tick += dt;
          uint8_t st = b[q];
          if (st & 0x80) q++; else st = run;
          if (st == 0xFF) {
            uint8_t ty = b[q++];
            uint32_t l = 0;
            while (q < end) { uint8_t c = b[q++]; l = (l << 7) | (c & 0x7F); if (!(c & 0x80)) break; }
            if (ty == 0x51 && l == 3) all.push_back({tick, 0xFF, 0, 0, (uint32_t)(b[q] << 16 | b[q + 1] << 8 | b[q + 2])});
            q += l;
          } else if (st == 0xF0 || st == 0xF7) {
            uint32_t l = 0;
            while (q < end) { uint8_t c = b[q++]; l = (l << 7) | (c & 0x7F); if (!(c & 0x80)) break; }
            q += l;
          } else {
            run = st;
            uint8_t ty = st & 0xF0, d1 = b[q++], d2 = 0;
            if (ty != 0xC0 && ty != 0xD0) d2 = b[q++];
            if (ty == 0x80 || ty == 0x90 || ty == 0xC0 || (ty == 0xB0 && (d1 == 120 || d1 == 123)))
              all.push_back({tick, st, d1, d2, 0});
          }
        }
        p = end;
      }
      std::stable_sort(all.begin(), all.end(), [](const RE& a, const RE& c) { return a.tick < c.tick; });
      uint32_t n = 0;
      for (auto& r : all) if (r.st != 0xFF) n++;
      ev = (E*)malloc(sizeof(E) * std::max<uint32_t>(n, 1));
      if (ev) {
        double us = 500000, sPos = 0;
        uint32_t lastTick = 0;
        nev = 0;
        for (auto& r : all) {
          sPos += (r.tick - lastTick) * us / div / 1e6 * SR;
          lastTick = r.tick;
          if (r.st == 0xFF) { us = r.tempo; continue; }
          ev[nev++] = {(uint32_t)sPos, r.st, r.d1, r.d2};
        }
        lenS = nev ? ev[nev - 1].s : 0;
        ok = nev > 0;
      }
    }
  }
  free(b);
  return ok;
}
}  // namespace MidiPlay

namespace Audio {
}  // namespace Audio

// ====================================================================== looper
namespace Looper {
using namespace Audio;
Track tr[TRACKS];
volatile bool playing = false, recording = false, counting = false;
volatile int recTrack = 0;
int bpm = 110, bars = 2, quant = 2;
bool metro = true;

static SemaphoreHandle_t lmux;
static double lpos = 0, countElapsed = 0;
static volatile uint16_t wrapCount = 0;
static uint16_t curSession = 0, sessionCtr = 0;
static bool held[128];

void lock() { xSemaphoreTake(lmux, portMAX_DELAY); }
void unlock() { xSemaphoreGive(lmux); }
uint32_t lenTicks() { return (uint32_t)bars * 4 * PPQN; }
float posTicks() { return (float)lpos; }
float countBeats() { return counting ? 4.f - (float)(countElapsed / PPQN) : 0.f; }

bool empty() {
  for (auto& t : tr)
    if (!t.ev.empty()) return false;
  return true;
}

static void insertEv(Track& t, const Ev& e) {
  auto it = std::upper_bound(t.ev.begin(), t.ev.end(), e.t, [](uint32_t v, const Ev& x) { return v < x.t; });
  t.ev.insert(it, e);
}

void play() {
  if (playing || counting) return;
  lpos = 0; wrapCount = 0; playing = true;
}
void stop() {
  playing = false; recording = false; counting = false;
  for (int i = 1; i <= TRACKS; i++) allOff(i);
}
void recNote(uint8_t note, bool on) {
  if (!recording) return;
  lock();
  uint32_t L = lenTicks();
  double t = lpos;
  uint16_t w = wrapCount;
  if (on && quant) {
    uint32_t g = quant == 1 ? PPQN / 2 : PPQN / 4;
    t = round(t / g) * g;
    if (t >= L) { t -= L; w++; }
  }
  Ev e{(uint32_t)t % L, note, (uint8_t)on, curSession, w};
  insertEv(tr[recTrack], e);
  held[note & 127] = on;
  unlock();
}
void toggleRec(int track) {
  if (recording || counting) {
    if (recording)
      for (int n = 0; n < 128; n++)
        if (held[n]) recNote(n, false);
    recording = false;
    counting = false;
    return;
  }
  recTrack = track;
  curSession = ++sessionCtr;
  tr[track].lastSession = curSession;
  memset(held, 0, sizeof(held));
  if (!playing) {
    countElapsed = 0;
    counting = true;
    click(true);
  } else {
    recording = true;
  }
}
void undo(int track) {
  lock();
  uint16_t s = tr[track].lastSession;
  auto& ev = tr[track].ev;
  ev.erase(std::remove_if(ev.begin(), ev.end(), [s](const Ev& e) { return e.session == s; }), ev.end());
  tr[track].lastSession = 0;
  unlock();
  allOff(track + 1);
}
void clear(int track) {
  lock();
  tr[track].ev.clear();
  unlock();
  allOff(track + 1);
}

bool save(const char* path) {
  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;
  lock();
  f.write((const uint8_t*)"WLP1", 4);
  uint16_t hdr[2] = {(uint16_t)bpm, (uint16_t)bars};
  f.write((uint8_t*)hdr, sizeof(hdr));
  for (auto& t : tr) {
    uint8_t m[2] = {t.wave, (uint8_t)t.mute};
    f.write(m, 2);
    uint32_t n = t.ev.size();
    f.write((uint8_t*)&n, 4);
    for (auto& e : t.ev) {
      uint8_t b[6];
      memcpy(b, &e.t, 4); b[4] = e.note; b[5] = e.on;
      f.write(b, 6);
    }
  }
  unlock();
  f.close();
  return true;
}
bool load(const char* path) {
  File f = SD.open(path);
  if (!f) return false;
  char mg[4];
  if (f.read((uint8_t*)mg, 4) != 4 || memcmp(mg, "WLP1", 4)) { f.close(); return false; }
  stop();
  lock();
  uint16_t hdr[2];
  f.read((uint8_t*)hdr, sizeof(hdr));
  bpm = constrain(hdr[0], 40, 240);
  bars = constrain(hdr[1], 1, 8);
  for (auto& t : tr) {
    uint8_t m[2];
    f.read(m, 2);
    t.wave = m[0] % W_COUNT; t.mute = m[1];
    uint32_t n = 0;
    f.read((uint8_t*)&n, 4);
    n = std::min<uint32_t>(n, 4000);
    t.ev.clear();
    t.ev.reserve(n);
    for (uint32_t i = 0; i < n; i++) {
      uint8_t b[6];
      if (f.read(b, 6) != 6) break;
      Ev e; memcpy(&e.t, b, 4); e.note = b[4]; e.on = b[5]; e.session = 0; e.wrap = 0;
      t.ev.push_back(e);
    }
    t.lastSession = 0;
  }
  unlock();
  f.close();
  return true;
}


// export all tracks as a Standard MIDI File (format 1, one MIDI track per loop track)
static void vlq(std::vector<uint8_t>& v, uint32_t x) {
  uint8_t b[5]; int n = 0;
  b[n++] = x & 0x7F;
  while (x >>= 7) b[n++] = 0x80 | (x & 0x7F);
  while (n) v.push_back(b[--n]);
}
bool exportMidi(const char* path, int repeats) {
  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;
  lock();
  int ntr = 1 + TRACKS;
  uint8_t hdr[14] = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, (uint8_t)ntr, 0, PPQN};
  f.write(hdr, 14);
  auto writeTrack = [&](std::vector<uint8_t>& d) {
    uint8_t th[8] = {'M', 'T', 'r', 'k', (uint8_t)(d.size() >> 24), (uint8_t)(d.size() >> 16), (uint8_t)(d.size() >> 8), (uint8_t)d.size()};
    f.write(th, 8);
    f.write(d.data(), d.size());
  };
  std::vector<uint8_t> d;
  uint32_t us = 60000000UL / bpm;
  d = {0x00, 0xFF, 0x51, 0x03, (uint8_t)(us >> 16), (uint8_t)(us >> 8), (uint8_t)us, 0x00, 0xFF, 0x2F, 0x00};
  writeTrack(d);
  uint32_t L = lenTicks();
  static const uint8_t prog[W_COUNT] = {80, 80, 81, 79, 122};
  for (int t = 0; t < TRACKS; t++) {
    d.clear();
    d.push_back(0); d.push_back(0xC0 | t); d.push_back(prog[tr[t].wave % W_COUNT]);
    uint32_t last = 0;
    for (int r = 0; r < repeats; r++)
      for (auto& e : tr[t].ev) {
        uint32_t at = r * L + e.t;
        vlq(d, at - last); last = at;
        d.push_back((e.on ? 0x90 : 0x80) | t); d.push_back(e.note & 127); d.push_back(e.on ? 100 : 0);
      }
    uint32_t end = repeats * L;
    vlq(d, end > last ? end - last : 0);
    d.push_back(0xFF); d.push_back(0x2F); d.push_back(0);
    writeTrack(d);
  }
  unlock();
  f.close();
  return true;
}

// called from the audio task once per chunk
static void chunk(int n) {
  double tps = bpm * (double)PPQN / (60.0 * SR);
  double adv = tps * n;
  if (counting) {
    int b0 = (int)(countElapsed / PPQN);
    countElapsed += adv;
    int b1 = (int)(countElapsed / PPQN);
    if (countElapsed >= 4 * PPQN) {
      counting = false; lpos = 0; wrapCount = 0; playing = true; recording = true;
      click(true);
    } else if (b1 != b0) {
      click(false);
    }
    return;
  }
  if (!playing) return;
  uint32_t L = lenTicks();
  double a = lpos, b = lpos + adv;
  if ((metro || recording) && (int)(a / PPQN) != (int)(b / PPQN)) {
    uint32_t beat = (uint32_t)(b / PPQN) % (L / PPQN);
    click(beat % 4 == 0);
  }
  if (xSemaphoreTake(lmux, pdMS_TO_TICKS(3)) == pdTRUE) {  // UI only holds it briefly
    for (int t = 0; t < TRACKS; t++) {
      if (tr[t].mute) continue;
      for (auto& e : tr[t].ev) {
        bool hit = (e.t >= a && e.t < b) || (b >= L && e.t < b - L);
        if (!hit) continue;
        uint16_t evWrap = (e.t < a) ? (uint16_t)(wrapCount + 1) : wrapCount;
        if (recording && t == recTrack && e.session == curSession && e.wrap == evWrap) continue;
        if (e.on) vOn(e.note, tr[t].wave, t + 1);
        else vOff(e.note, t + 1);
      }
    }
    xSemaphoreGive(lmux);
  }
  lpos = b;
  if (lpos >= L) { lpos -= L; wrapCount++; }
}
}  // namespace Looper

// ====================================================================== music
// WAV is streamed directly. MP3 / FLAC / AAC are decoded by ESP8266Audio in a
// separate task into a ring buffer, and the mixer resamples from that ring.
namespace Music {
using namespace Audio;
struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  inline float p(float x) {
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
  void shelf(bool high, float f0, float db) {
    float A = powf(10.f, db / 40.f), w = 2 * PI * f0 / SR, c = cosf(w), s = sinf(w);
    float al = s / 2 * sqrtf(2.f), sa = 2 * sqrtf(A) * al, a0;
    if (!high) {
      b0 = A * ((A + 1) - (A - 1) * c + sa); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - sa);
      a0 = (A + 1) + (A - 1) * c + sa; a1 = -2 * ((A - 1) + (A + 1) * c); a2 = (A + 1) + (A - 1) * c - sa;
    } else {
      b0 = A * ((A + 1) + (A - 1) * c + sa); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - sa);
      a0 = (A + 1) - (A - 1) * c + sa; a1 = 2 * ((A - 1) - (A + 1) * c); a2 = (A + 1) - (A - 1) * c - sa;
    }
    b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
  }
};

enum Fmt { F_NONE, F_WAV, F_MP3, F_FLAC, F_AAC };
static SemaphoreHandle_t mmux, decMux;
static File mf;
static volatile bool mActive = false, mPaused = false, mEnded = false;
static volatile int fmt = F_NONE;
static String curPath;
static uint32_t dataStart = 0, totalFrames = 0;
static int mRate = 44100, mCh = 2, mBits = 16, frameBytes = 4;
static double fpos = 0;
static float mSpeed = 1, tgtGain = 1, curGain = 1, preamp = 1;
static int16_t inBuf[1024];
static uint8_t raw[1024 * 6];
static uint32_t bufStart = 0, bufFrames = 0;
static Biquad bass, treb;
static volatile bool eqDirty = false;
static float eqB = 0, eqT = 0;
static double abA_ = -1, abB_ = -1;

// ---- compressed path
constexpr uint32_t RING = 8192;  // power of two
static int16_t ring[RING];
static volatile uint32_t rHead = 0, rTail = 0;
static volatile int codecRate = 44100;
static volatile bool decDone = false;
volatile uint32_t underruns = 0;
static volatile uint32_t skipSamples = 0;  // FLAC seek: decode and discard
static double cFrac = 0;
static uint64_t consumed = 0;
static double posBase = 0, durSec = 0;
static uint32_t audioOff = 0, fileSize = 0;
static AudioFileSourceSD* src = nullptr;
static AudioFileSourceID3* id3 = nullptr;
static AudioGenerator* gen = nullptr;
static void* mp3Arena = nullptr;  // MP3 decoder memory, reserved once at boot (avoids out-of-memory later)
String lastErr;

class RingOut : public AudioOutput {
 public:
  bool begin() override { return true; }
  bool SetRate(int hz) override { hertz = hz; codecRate = hz; return true; }
  bool SetChannels(int c) override { channels = c; return true; }
  bool ConsumeSample(int16_t s[2]) override {
    if (skipSamples) { skipSamples--; return true; }
    if (rHead - rTail >= RING - 1) return false;
    ring[rHead & (RING - 1)] = (int16_t)(((int32_t)s[0] + s[1]) / 2);
    rHead = rHead + 1;
    return true;
  }
  bool stop() override { return true; }
};
static RingOut ringOut;

static void freeCodec() {
  if (gen) { gen->stop(); delete gen; gen = nullptr; }
  if (id3) { delete id3; id3 = nullptr; }
  if (src) { delete src; src = nullptr; }
}
static bool startCodec(uint32_t byteOff, uint32_t skip) {
  freeCodec();
  src = new AudioFileSourceSD(curPath.c_str());
  if (!src->isOpen()) { freeCodec(); lastErr = "FILE WON'T OPEN"; return false; }
  // jump straight past the ID3 tag (the library's ID3 reader walks it 1 byte at a time:
  // with big embedded cover art that took seconds between songs)
  src->seek(std::max(byteOff, audioOff), SEEK_SET);
  if (fmt == F_MP3) gen = mp3Arena ? new AudioGeneratorMP3(mp3Arena, AudioGeneratorMP3::preAllocSize()) : new AudioGeneratorMP3();
  else if (fmt == F_FLAC) gen = new AudioGeneratorFLAC();
  else gen = new AudioGeneratorAAC();
  rHead = rTail = 0; cFrac = 0; consumed = 0;
  skipSamples = skip;
  decDone = false;
  if (!gen->begin(src, &ringOut)) {
    char b[48]; snprintf(b, sizeof(b), "DECODER NO MEMORY (%uK FREE)", (unsigned)(ESP.getMaxAllocHeap() / 1024));
    lastErr = b;
    freeCodec(); return false;
  }
  return true;
}

static void decTask(void*) {
  for (;;) {
    bool busy = false;
    if (xSemaphoreTake(decMux, pdMS_TO_TICKS(20)) == pdTRUE) {
      if (gen && !decDone && fmt >= F_MP3) {
        if (rHead - rTail < RING - 2400 || skipSamples) {
          if (gen->isRunning()) { if (!gen->loop()) { gen->stop(); decDone = true; } }
          else decDone = true;
          busy = true;
        }
      }
      xSemaphoreGive(decMux);
    }
    // always block a little: lets the idle task run (task watchdog) and saves power
    vTaskDelay(busy ? 1 : 5);
  }
}

// --- header probes for duration
static const uint16_t MP3_BR[2][16] = {{0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0},
                                       {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0}};
static const uint32_t MP3_SR[3][3] = {{44100, 48000, 32000}, {22050, 24000, 16000}, {11025, 12000, 8000}};
static uint32_t id3Size(File& f) {
  uint8_t h[10];
  f.seek(0);
  if (f.read(h, 10) == 10 && !memcmp(h, "ID3", 3))
    return 10 + ((h[6] & 0x7F) << 21 | (h[7] & 0x7F) << 14 | (h[8] & 0x7F) << 7 | (h[9] & 0x7F)) + ((h[5] & 0x10) ? 10 : 0);
  return 0;
}
static void probeMP3(File& f) {
  audioOff = id3Size(f);
  f.seek(audioOff);
  uint8_t b[2048];
  int n = f.read(b, sizeof(b));
  for (int i = 0; i + 4 < n; i++) {
    if (b[i] != 0xFF || (b[i + 1] & 0xE0) != 0xE0) continue;
    int ver = (b[i + 1] >> 3) & 3;  // 3 = MPEG1, 2 = MPEG2, 0 = MPEG2.5
    int brI = b[i + 2] >> 4, srI = (b[i + 2] >> 2) & 3;
    if (ver == 1 || brI == 0 || brI == 15 || srI == 3) continue;
    int vi = ver == 3 ? 0 : (ver == 2 ? 1 : 2);
    uint32_t sr = MP3_SR[vi][srI];
    uint32_t br = MP3_BR[ver == 3 ? 0 : 1][brI] * 1000;
    mRate = sr;
    int spf = ver == 3 ? 1152 : 576;
    // Xing / Info header for VBR files
    for (int k = i + 4; k < i + 64 && k + 12 < n; k++) {
      if (!memcmp(b + k, "Xing", 4) || !memcmp(b + k, "Info", 4)) {
        if (b[k + 7] & 1) {
          uint32_t frames = (b[k + 8] << 24) | (b[k + 9] << 16) | (b[k + 10] << 8) | b[k + 11];
          durSec = (double)frames * spf / sr;
          return;
        }
      }
    }
    durSec = br ? (double)(fileSize - audioOff) * 8 / br : 0;
    return;
  }
}
static void probeFLAC(File& f) {
  audioOff = id3Size(f);
  f.seek(audioOff);
  uint8_t b[42];
  if (f.read(b, 42) == 42 && !memcmp(b, "fLaC", 4)) {
    const uint8_t* si = b + 8;
    uint32_t sr = (si[10] << 12) | (si[11] << 4) | (si[12] >> 4);
    uint64_t tot = ((uint64_t)(si[13] & 0x0F) << 32) | ((uint32_t)si[14] << 24) | (si[15] << 16) | (si[16] << 8) | si[17];
    mBits = ((si[12] & 1) << 4 | (si[13] >> 4)) + 1;
    mRate = sr ? sr : 44100;
    durSec = sr ? (double)tot / sr : 0;
  }
}
static void probeAAC(File& f) {
  audioOff = id3Size(f);
  f.seek(audioOff);
  uint8_t b[16];
  static const uint32_t AAC_SR[13] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350};
  if (f.read(b, 16) >= 7 && b[0] == 0xFF && (b[1] & 0xF0) == 0xF0) {
    int sri = (b[2] >> 2) & 0xF;
    uint32_t flen = ((b[3] & 3) << 11) | (b[4] << 3) | (b[5] >> 5);
    mRate = sri < 13 ? AAC_SR[sri] : 44100;
    if (flen) durSec = (double)(fileSize - audioOff) / flen * 1024 / mRate;
  }
}

// --- WAV path
static bool fill(uint32_t ip) {
  if (ip >= totalFrames) return false;
  uint32_t want = std::min<uint32_t>(1024, totalFrames - ip);
  uint32_t off = dataStart + ip * frameBytes;
  if (mf.position() != off) mf.seek(off);
  size_t got = mf.read(raw, want * frameBytes);
  uint32_t n = got / frameBytes;
  int bps = mBits / 8;
  for (uint32_t i = 0; i < n; i++) {
    int32_t acc = 0;
    for (int c = 0; c < mCh; c++) {
      const uint8_t* p = raw + i * frameBytes + c * bps;
      int32_t s;
      if (bps == 1) s = ((int32_t)p[0] - 128) << 8;
      else if (bps == 2) s = (int16_t)(p[0] | (p[1] << 8));
      else s = (int16_t)(p[1] | (p[2] << 8));
      acc += s;
    }
    inBuf[i] = acc / mCh;
  }
  bufStart = ip;
  bufFrames = n;
  return n > 0;
}

static float fade = 1, fadeTgt = 1;      // short fade for pause/resume (no clicks)
static volatile bool pauseReq = false;
static inline void out1(float* out, int i, float s) {
  fade += (fadeTgt - fade) * 0.004f;
  s *= fade;
  s = treb.p(bass.p(s)) * preamp;
  curGain += (tgtGain - curGain) * 0.0005f;
  s *= curGain;
  out[i] += s;
  visMus[visMusI] = (int16_t)constrain(s * 32767.f, -32767.f, 32767.f);
  visMusI = (visMusI + 1) % VIS;
}

static void render(float* out, int n) {
  if (eqDirty) {
    bass.shelf(false, 160, eqB); treb.shelf(true, 3500, eqT);
    preamp = powf(10.f, -std::max(0.f, std::max(eqB, eqT)) * 0.6f / 20.f);
    eqDirty = false;
  }
  if (!mActive || mPaused) return;
  if (pauseReq && fade < 0.01f) { mPaused = true; pauseReq = false; return; }
  if (fmt == F_WAV) {
    double step = (double)mRate / SR * mSpeed;
    for (int i = 0; i < n; i++) {
      uint32_t ip = (uint32_t)fpos;
      if (ip + 1 >= totalFrames) { mEnded = true; mPaused = true; break; }
      if (ip < bufStart || ip + 1 >= bufStart + bufFrames) {
        if (!fill(ip) || bufFrames < 2) { mEnded = true; mPaused = true; break; }
      }
      float fr = (float)(fpos - ip);
      out1(out, i, (inBuf[ip - bufStart] * (1 - fr) + inBuf[ip + 1 - bufStart] * fr) / 32768.f);
      fpos += step;
      if (abB_ > 0 && fpos >= abB_ * mRate) fpos = abA_ * mRate;
    }
    return;
  }
  // compressed: pull from ring
  if (skipSamples) return;  // still fast-forwarding
  double step = (double)codecRate / SR * mSpeed;
  for (int i = 0; i < n; i++) {
    while (cFrac >= 1 && rHead - rTail >= 2) { rTail = rTail + 1; consumed++; cFrac -= 1; }
    if (rHead - rTail < 2) {
      if (decDone) { mEnded = true; mPaused = true; }
      else underruns++;
      break;  // underrun: wait for the decoder
    }
    float a = ring[rTail & (RING - 1)], b = ring[(rTail + 1) & (RING - 1)];
    out1(out, i, (a * (1 - (float)cFrac) + b * (float)cFrac) / 32768.f);
    cFrac += step;
  }
}

static double codecPos() { return posBase + (double)consumed / std::max(1, (int)codecRate); }

bool open(const char* path, float startSec) {
  xSemaphoreTake(mmux, portMAX_DELAY);
  xSemaphoreTake(decMux, portMAX_DELAY);
  freeCodec();
  if (mf) mf.close();
  mActive = false;
  fmt = F_NONE;
  bool ok = false;
  curPath = path;
  String low = curPath; low.toLowerCase();
  mf = SD.open(path);
  fileSize = mf ? mf.size() : 0;
  lastErr = mf ? "NOT A SUPPORTED FORMAT" : "FILE NOT FOUND";
  mBits = 16; mCh = 2; durSec = 0;
  if (mf && (low.endsWith(".mp3") || low.endsWith(".flac") || low.endsWith(".aac"))) {
    fmt = low.endsWith(".mp3") ? F_MP3 : (low.endsWith(".flac") ? F_FLAC : F_AAC);
    if (fmt == F_MP3) probeMP3(mf); else if (fmt == F_FLAC) probeFLAC(mf); else probeAAC(mf);
    mf.close();
    codecRate = mRate;
    posBase = 0;
    uint32_t off = 0, skip = 0;
    if (startSec > 1 && durSec > 0) {
      if (fmt == F_FLAC) skip = (uint32_t)(startSec * mRate);
      else off = audioOff + (uint32_t)((fileSize - audioOff) * (startSec / durSec));
      posBase = startSec;
    }
    ok = startCodec(off, skip);
  } else if (mf) {
    fmt = F_WAV;
    uint8_t h[12];
    if (mf.read(h, 12) == 12 && !memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WAVE", 4)) {
      bool fmtOk = false;
      while (mf.available() >= 8) {
        uint8_t ck[8];
        mf.read(ck, 8);
        uint32_t sz = ck[4] | (ck[5] << 8) | (ck[6] << 16) | ((uint32_t)ck[7] << 24);
        if (!memcmp(ck, "fmt ", 4)) {
          uint8_t fm[16] = {0};
          mf.read(fm, std::min<uint32_t>(16, sz));
          if (sz > 16) mf.seek(mf.position() + sz - 16 + (sz & 1));
          uint16_t wf = fm[0] | (fm[1] << 8);
          mCh = fm[2] | (fm[3] << 8);
          mRate = fm[4] | (fm[5] << 8) | (fm[6] << 16) | ((uint32_t)fm[7] << 24);
          mBits = fm[14] | (fm[15] << 8);
          fmtOk = (wf == 1 || wf == 0xFFFE) && mCh >= 1 && mCh <= 2 &&
                  (mBits == 8 || mBits == 16 || mBits == 24) && mRate >= 8000 && mRate <= 96000;
        } else if (!memcmp(ck, "data", 4)) {
          dataStart = mf.position();
          uint32_t remain = mf.size() - dataStart;
          if (sz == 0 || sz > remain) sz = remain;
          frameBytes = mCh * mBits / 8;
          totalFrames = fmtOk ? sz / frameBytes : 0;
          ok = fmtOk && totalFrames > 2;
          break;
        } else {
          mf.seek(mf.position() + sz + (sz & 1));
        }
      }
    }
    if (ok) {
      durSec = (double)totalFrames / mRate;
      fpos = std::min<double>(startSec * mRate, totalFrames > 10 ? totalFrames - 10 : 0);
      bufFrames = 0; bufStart = 0;
    } else mf.close();
  }
  if (ok) {
    mPaused = false; mEnded = false; mActive = true;
    pauseReq = false; fade = 0; fadeTgt = 1;
    abA_ = abB_ = -1;
    curGain = 0;  // short fade-in, no click
  } else fmt = F_NONE;
  xSemaphoreGive(decMux);
  xSemaphoreGive(mmux);
  return ok;
}
void close() {
  xSemaphoreTake(mmux, portMAX_DELAY);
  xSemaphoreTake(decMux, portMAX_DELAY);
  mActive = false;
  freeCodec();
  if (mf) mf.close();
  fmt = F_NONE;
  xSemaphoreGive(decMux);
  xSemaphoreGive(mmux);
}
void pause(bool p) {
  if (p) { if (!mPaused) { fadeTgt = 0; pauseReq = true; } }
  else { pauseReq = false; fade = 0; fadeTgt = 1; mPaused = false; }
}
bool paused() { return mPaused || pauseReq; }
bool active() { return mActive; }
float pos() {
  if (!mActive) return 0;
  return fmt == F_WAV ? (float)(fpos / mRate) : (float)codecPos();
}
float len() { return mActive ? (float)durSec : 0; }
void seek(float sec) {
  if (!mActive) return;
  if (sec < 0) sec = 0;
  if (durSec > 0 && sec > durSec - 1) sec = std::max(0.0, durSec - 1);
  xSemaphoreTake(mmux, portMAX_DELAY);
  if (fmt == F_WAV) {
    double f = sec * (double)mRate;
    if (f > totalFrames - 2) f = totalFrames - 2;
    fpos = f;
  } else {
    xSemaphoreTake(decMux, portMAX_DELAY);
    bool wasEnded = mEnded;
    if (fmt == F_FLAC) startCodec(0, (uint32_t)(sec * codecRate));
    else startCodec(durSec > 0 ? audioOff + (uint32_t)((fileSize - audioOff) * (sec / durSec)) : 0, 0);
    posBase = sec;
    if (!gen) { mEnded = true; mPaused = true; }
    else if (wasEnded) mEnded = false;
    xSemaphoreGive(decMux);
  }
  xSemaphoreGive(mmux);
}
void setSpeed(float s) { mSpeed = constrain(s, 0.5f, 2.0f); }
float speed() { return mSpeed; }
void setEQ(float b, float t) { eqB = b; eqT = t; eqDirty = true; }
void setGain(float g) { tgtGain = g; }
bool takeEnded() { if (mEnded) { mEnded = false; return true; } return false; }
int srcRate() { return fmt == F_WAV ? mRate : codecRate; }
int channels() { return mCh; }
int bits() { return mBits; }
const char* error() { return lastErr.c_str(); }
uint32_t underrunCount() { return underruns; }
const char* format() {
  switch (fmt) { case F_WAV: return "WAV"; case F_MP3: return "MP3"; case F_FLAC: return "FLAC"; case F_AAC: return "AAC"; }
  return "";
}
bool seeking() { return skipSamples > 0; }
void abPress() {
  if (!mActive) return;
  if (abA_ < 0) abA_ = pos();
  else if (abB_ < 0) { abB_ = pos(); if (abB_ <= abA_ + 0.2) { abA_ = abB_ = -1; } }
  else { abA_ = abB_ = -1; }
}
int abState() { return abA_ < 0 ? 0 : (abB_ < 0 ? 1 : 2); }
float abA() { return abA_; }
float abB() { return abB_; }
// A-B for compressed formats is handled by seeking from the UI
void abTick() {
  if (fmt >= F_MP3 && abB_ > 0 && mActive && !mPaused && pos() >= abB_) seek(abA_);
}
}  // namespace Music

// ====================================================================== mixer task
namespace Audio {
static float svLow = 0, svBand = 0;

static void renderVoices(int off, int n) {
  float atk = 1.f / (std::max(sp.a, 0.002f) * SR);
  float dec = (1.f - sp.s) / (std::max(sp.d, 0.005f) * SR);
  float rel = 1.f / (std::max(sp.r, 0.005f) * SR);
  for (auto& v : V) {
    if (!v.on) continue;
    bool sfx = v.owner == OWNER_SFX;
    float a = sfx ? 1.f / (0.002f * SR) : atk;
    float d = sfx ? 0 : dec;
    float sus = sfx ? 1.f : sp.s;
    float r = sfx ? 1.f / (0.03f * SR) : rel;
    float g = sfx ? 0.28f * v.vel : 0.22f * sp.vol * v.vel;
    float* dst = sfx ? mix : syn;
    for (int i = off; i < off + n; i++) {
      switch (v.stage) {
        case 1: v.env += a; if (v.env >= 1) { v.env = 1; v.stage = 2; } break;
        case 2: v.env -= d; if (v.env <= sus) { v.env = sus; v.stage = 3; } break;
        case 3: v.env = sus; break;
        default: v.env -= r; if (v.env <= 0) { v.env = 0; v.on = false; } break;
      }
      if (v.sfxLeft > 0 && --v.sfxLeft == 0) v.stage = 4;
      float p = v.phase, s;
      switch (v.wave) {
        case W_SINE: { float f = p * 256; int k = (int)f; s = sineTab[k] + (sineTab[k + 1] - sineTab[k]) * (f - k); } break;
        case W_SQUARE: s = p < 0.5f ? 0.6f : -0.6f; break;
        case W_SAW: s = (2 * p - 1) * 0.7f; break;
        case W_TRI: s = p < 0.5f ? 4 * p - 1 : 3 - 4 * p; break;
        default: v.rng = v.rng * 1664525u + 1013904223u; s = (int16_t)(v.rng >> 16) / 65536.f; break;
      }
      v.phase += v.inc;
      if (v.phase >= 1) v.phase -= 1;
      dst[i] += s * v.env * g;
      if (!v.on) break;
    }
  }
  if (clickLeft > 0) {
    for (int i = off; i < off + n && clickLeft > 0; i++, clickLeft--) {
      mix[i] += sinf(clickPh * 2 * PI) * 0.35f * (clickLeft / (float)(SR / 30));
      clickPh += clickInc;
      if (clickPh >= 1) clickPh -= 1;
    }
  }
}

static void render(int16_t* out) {
  memset(mix, 0, sizeof(mix));
  memset(syn, 0, sizeof(syn));
  processCmds();
  if (xSemaphoreTake(Music::mmux, pdMS_TO_TICKS(5)) == pdTRUE) {
    Music::render(mix, BLOCK);
    xSemaphoreGive(Music::mmux);
  }
  for (int off = 0; off < BLOCK; off += CHUNK) {
    Looper::chunk(CHUNK);
    Drums::chunk(CHUNK);
    MidiPlay::chunk(CHUNK);
    arpChunk(CHUNK, Looper::bpm);
    if (metroBpm > 0) {  // standalone metronome (guitar tools)
      metroAcc += CHUNK;
      double spb = SR * 60.0 / metroBpm;
      if (metroAcc >= spb) { metroAcc -= spb; click(metroBeat % std::max(1, (int)metroBeats) == 0); metroBeat++; metroFlash = millis(); }
    }
    renderVoices(off, CHUNK);
    drumRender(mix, off, CHUNK);
  }
  // synth bus: resonant low-pass + tempo-synced delay (skipped when silent for a while to save power)
  static uint32_t silentBlocks = 0;
  bool anyVoice = false;
  for (auto& v : V) if (v.on && v.owner != OWNER_SFX) { anyVoice = true; break; }
  silentBlocks = anyVoice ? 0 : silentBlocks + 1;
  if (silentBlocks > 200) {  // ~3 s of silence: delay tail has died out
    for (int i = 0; i < BLOCK; i++) {
      float m = mix[i];
      if (m > 0.85f) m = 0.85f + (m - 0.85f) / (1 + (m - 0.85f) * 6);
      if (m < -0.85f) m = -0.85f + (m + 0.85f) / (1 - (m + 0.85f) * 6);
      out[i] = (int16_t)constrain(m * 32767.f, -32767.f, 32767.f);
    }
    return;
  }
  float fc = std::min(60.f * powf(2.f, sp.cutoff * 7.5f), 5000.f);
  float f = 2 * sinf(PI * fc / SR);
  float q = 1.8f * (1 - sp.reso) + 0.08f;
  int dlen = constrain((int)(SR * 30.f / Looper::bpm), 800, DL - 1);  // 1/8 note (capped)
  for (int i = 0; i < BLOCK; i++) {
    svLow += f * svBand;
    float hi = syn[i] - svLow - q * svBand;
    svBand += f * hi;
    float s = svLow;
    if (dline) {
      int ri = dIdx - dlen; if (ri < 0) ri += DL;
      float d = dline[ri] / 32768.f;
      s += d * sp.delayMix;
      dline[dIdx] = (int16_t)constrain((svLow + d * sp.delayFb) * 32767.f, -32767.f, 32767.f);
      dIdx = (dIdx + 1) % DL;
    }
    visSyn[visSynI] = (int16_t)constrain(s * 32767.f, -32767.f, 32767.f);
    visSynI = (visSynI + 1) % VIS;
    float m = mix[i] + s;
    if (m > 0.85f) m = 0.85f + (m - 0.85f) / (1 + (m - 0.85f) * 6);   // soft clip
    if (m < -0.85f) m = -0.85f + (m + 0.85f) / (1 - (m + 0.85f) * 6);
    out[i] = (int16_t)constrain(m * 32767.f, -32767.f, 32767.f);
  }
}

static volatile bool suspendReq = false, suspended = false;
static void task(void*) {
  int bi = 0;
  for (;;) {
    if (suspendReq) { suspended = true; vTaskDelay(10); continue; }
    suspended = false;
    while (M5.Speaker.isPlaying(0) >= 2) vTaskDelay(1);
    render(outBuf[bi]);
    M5.Speaker.playRaw(outBuf[bi], BLOCK, SR, false, 1, 0);
    bi = (bi + 1) % 3;
  }
}

void begin() {
  Music::mp3Arena = malloc(AudioGeneratorMP3::preAllocSize());
  for (int i = 0; i <= 256; i++) sineTab[i] = sinf(2 * PI * i / 256.f) * 0.8f;
  cmdQ = xQueueCreate(64, sizeof(Cmd));
  Music::mmux = xSemaphoreCreateMutex();
  Music::decMux = xSemaphoreCreateMutex();
  Looper::lmux = xSemaphoreCreateMutex();
  M5.Speaker.begin();
  xTaskCreatePinnedToCore(task, "audio", 8192, nullptr, 3, nullptr, 0);
  xTaskCreatePinnedToCore(Music::decTask, "decoder", 12288, nullptr, 2, nullptr, 0);
}

void suspend(bool s) {
  if (s) {
    suspendReq = true;
    for (int i = 0; i < 100 && !suspended; i++) delay(5);
    M5.Speaker.stop();
    M5.Speaker.end();
  } else {
    M5.Speaker.begin();
    M5.Speaker.setVolume(lastVol);
    suspendReq = false;
  }
}
void metronome(int bpm, int beats) { metroBeats = beats; metroAcc = 0; metroBeat = 0; metroBpm = bpm; }
int metroBeatNow() { return metroBeat; }
uint32_t metroFlashT() { return metroFlash; }
}  // namespace Audio
