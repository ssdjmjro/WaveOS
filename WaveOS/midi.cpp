// WAVE OS - MIDI app
//  LIVE   : keyboard -> MIDI out (USB + Bluetooth), CC knobs, program change, channel, chords
//  PLAYER : play .mid files from /midi through the built-in synth + drum machine
//  IN     : MIDI from USB or Bluetooth plays the synth in every app and records into the looper
#include "common.h"
#include "audio.h"
#if !ARDUINO_USB_MODE  // USB MIDI only exists in TinyUSB mode
#include "USB.h"
#include "USBMIDI.h"
#define HAS_USB_MIDI 1
#else
#define HAS_USB_MIDI 0
#endif
#include <NimBLEDevice.h>
#include <algorithm>

#if HAS_USB_MIDI
static USBMIDI MIDI("WAVE OS");
#endif
static bool localSound = true;
static uint32_t rxFlash = 0, txFlash = 0;
static String logLines[8];
static int logN = 0;
static uint8_t inHeld[128];

static void logMsg(const char* dir, const char* m) {
  for (int i = 7; i > 0; i--) logLines[i] = logLines[i - 1];
  logLines[0] = String(dir) + " " + m;
  if (logN < 8) logN++;
}
static const char* noteName(uint8_t n) {
  static const char* nm[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  static char b[6];
  snprintf(b, sizeof(b), "%s%d", nm[n % 12], n / 12 - 1);
  return b;
}

// ===================================================================== Bluetooth LE MIDI
namespace BleMidi {
static bool on = false;
static volatile bool connected = false;
static NimBLECharacteristic* chr = nullptr;
static QueueHandle_t rxQ = nullptr;
struct Msg { uint8_t st, d1, d2; };
static const char* SVC = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
static const char* CHR = "7772E5DB-3868-4112-A1A9-F2669D106BF3";

class SrvCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo&) override { connected = true; }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override { connected = false; NimBLEDevice::startAdvertising(); }
};
class ChrCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    std::string v = c->getValue();
    const uint8_t* p = (const uint8_t*)v.data();
    size_t n = v.size(), i = 1;  // byte 0 = header
    uint8_t run = 0;
    while (i < n) {
      if (p[i] & 0x80) {  // timestamp byte, maybe followed by status
        i++;
        if (i < n && (p[i] & 0x80)) run = p[i++];
      }
      if (!run || i >= n) break;
      uint8_t ty = run & 0xF0;
      int len = (ty == 0xC0 || ty == 0xD0) ? 1 : 2;
      if (i + len > n) break;
      Msg m{run, p[i], len == 2 ? p[i + 1] : (uint8_t)0};
      i += len;
      if (rxQ) xQueueSend(rxQ, &m, 0);
    }
  }
};
void begin() {
  if (on) return;
  if (ESP.getMaxAllocHeap() < 40000) { toast("NOT ENOUGH RAM FOR BT"); return; }
  if (!rxQ) rxQ = xQueueCreate(64, sizeof(Msg));
  NimBLEDevice::init("WAVE OS");
  NimBLEServer* srv = NimBLEDevice::createServer();
  srv->setCallbacks(new SrvCB());
  NimBLEService* s = srv->createService(SVC);
  chr = s->createCharacteristic(CHR, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY);
  chr->setCallbacks(new ChrCB());
  s->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SVC);
  adv->setName("WAVE OS");
  adv->start();
  on = true;
}
void end() {
  if (!on) return;
  NimBLEDevice::deinit(true);
  chr = nullptr; on = false; connected = false;
}
void send(uint8_t st, uint8_t d1, uint8_t d2) {
  if (!on || !connected || !chr) return;
  uint32_t t = millis() & 0x1FFF;
  uint8_t pk[5] = {(uint8_t)(0x80 | (t >> 7)), (uint8_t)(0x80 | (t & 0x7F)), st, d1, d2};
  bool two = (st & 0xF0) != 0xC0 && (st & 0xF0) != 0xD0;
  chr->setValue(pk, two ? 5 : 4);
  chr->notify();
}
}  // namespace BleMidi

// unified MIDI out (USB + BLE)
static void midiOut(uint8_t st, uint8_t d1, uint8_t d2) {
  uint8_t ty = st & 0xF0, ch = (st & 15) + 1;
#if HAS_USB_MIDI
  if (ty == 0x90) MIDI.noteOn(d1, d2, ch);
  else if (ty == 0x80) MIDI.noteOff(d1, 0, ch);
  else if (ty == 0xB0) MIDI.controlChange(d1, d2, ch);
  else if (ty == 0xC0) MIDI.programChange(d1, ch);
#else
  (void)ty; (void)ch;
#endif
  BleMidi::send(st, d1, d2);
  txFlash = millis();
}

static void handleIn(uint8_t stb, uint8_t d1, uint8_t d2) {
  uint8_t st = stb & 0xF0, ch = (stb & 0x0F) + 1;
  char m[32];
  rxFlash = millis();
  if (st == 0x90 && d2 > 0) {
    if (ch == 10) Audio::drumHit(d1 <= 36 ? 0 : d1 <= 40 ? 1 : d1 <= 44 ? 2 : 3, d2 / 127.f);
    else if (localSound) Audio::noteOn(d1, Looper::tr[Looper::recTrack].wave, Audio::OWNER_MIDI);
    if (ch != 10) Looper::recNote(d1, true);
    inHeld[d1 & 127] = 1;
    snprintf(m, sizeof(m), "ON  %-4s V%-3d CH%d", noteName(d1), d2, ch);
    logMsg("<", m);
  } else if (st == 0x80 || (st == 0x90 && d2 == 0)) {
    Audio::noteOff(d1, Audio::OWNER_MIDI);
    if (inHeld[d1 & 127]) Looper::recNote(d1, false);
    inHeld[d1 & 127] = 0;
    snprintf(m, sizeof(m), "OFF %-4s CH%d", noteName(d1), ch);
    logMsg("<", m);
  } else if (st == 0xB0) {
    float v = d2 / 127.f;
    if (d1 == 74) Audio::sp.cutoff = v;
    else if (d1 == 71) Audio::sp.reso = v * 0.95f;
    else if (d1 == 7) Audio::sp.vol = v;
    else if (d1 == 91) Audio::sp.delayMix = v * 0.8f;
    else if (d1 == 123) Audio::allOff(Audio::OWNER_MIDI);
    snprintf(m, sizeof(m), "CC%-3d = %d", d1, d2);
    logMsg("<", m);
  } else if (st == 0xC0) {
    Looper::tr[Looper::recTrack].wave = d1 % Audio::W_COUNT;
    snprintf(m, sizeof(m), "PROGRAM %d", d1);
    logMsg("<", m);
  }
}

// called every frame from loop()
void midiTick() {
  int guard = 0;
#if HAS_USB_MIDI
  midiEventPacket_t p;
  while (MIDI.readPacket(&p) && guard++ < 64) handleIn(p.byte1, p.byte2, p.byte3);
#endif
  BleMidi::Msg m;
  while (BleMidi::rxQ && xQueueReceive(BleMidi::rxQ, &m, 0) == pdTRUE && guard++ < 128) handleIn(m.st, m.d1, m.d2);
}

struct MidiApp : App {
  enum { LIVE, PLAYER, NPAGE };
  int page = LIVE;
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
  int octave = 4, vel = 100, program = 0, cc = 0, chord = 0;
  uint8_t ccVal[4] = {0, 64, 0, 100};
  const uint8_t ccNum[4] = {1, 74, 71, 7};
  const char* ccName[4] = {"MOD", "CUTOFF", "RESO", "VOLUME"};
  const char* CHORD[4] = {"", "MAJ", "MIN", "7TH"};
  // player
  std::vector<String> files;
  int fsel = 0;
  String loaded;
  MidiApp() { name = "MIDI"; }
  bool wantsAllKeys() override { return page == LIVE; }
  void scan() {
    files.clear();
    File d = SD.open("/midi");
    if (!d) return;
    while (true) {
      File f = d.openNextFile();
      if (!f) break;
      String n = f.name(), l = n;
      l.toLowerCase();
      if (!f.isDirectory() && (l.endsWith(".mid") || l.endsWith(".midi"))) files.push_back(n);
      f.close();
    }
    d.close();
    if (sdOK && SD.exists("/loops/jam.mid")) files.push_back("../loops/jam.mid");
    std::sort(files.begin(), files.end());
  }
  void enter() override { Audio::useDelay(); ensureDir("/midi"); scan(); }
  String fullPath(int i) { return files[i].startsWith("../") ? "/" + files[i].substring(3) : "/midi/" + files[i]; }
  void leave() override {
    for (auto& h : held) for (auto n : h.notes) { midiOut(0x80 | (cfg.midiCh - 1), n, 0); Audio::noteOff(n, 0); }
    held.clear();
  }
  bool key(const KeyEv& e) override {
    if (e.code == K_TAB) { leave(); page = (page + 1) % NPAGE; return true; }
    if (e.fn && e.code == K_CHAR && tolower(e.ch) == 'b') return false;  // global screen-off
    if (e.code == K_CHAR && e.ch == '`') return false;
    if (page == PLAYER) {
      KeyCode k = e.nav();
      int n = files.size();
      if (k == K_UP && n) { fsel = (fsel + n - 1) % n; return true; }
      if (k == K_DOWN && n) { fsel = (fsel + 1) % n; return true; }
      if ((k == K_ENTER || (e.code == K_CHAR && e.ch == ' ')) && n) {
        String p = fullPath(fsel);
        if (MidiPlay::playing && loaded == p) { MidiPlay::stop(); return true; }
        toast("LOADING...", 500);
        if (MidiPlay::load(p.c_str())) { loaded = p; MidiPlay::play(); }
        else toast("CAN'T READ THIS MIDI FILE");
        return true;
      }
      if (e.is('s')) { MidiPlay::stop(); return true; }
      if (e.is('b')) { if (BleMidi::on) BleMidi::end(); else BleMidi::begin(); return true; }
      return false;
    }
    if (e.code == K_CHAR && !e.fn && semitone(tolower(e.ch)) >= 0) return true;
    if (e.code != K_CHAR) return false;
    char c = tolower(e.ch);
    char b[24];
    switch (c) {
      case 'z': octave = std::max(0, octave - 1); return true;
      case 'x': octave = std::min(8, octave + 1); return true;
      case 'c': cfg.midiCh = cfg.midiCh % 16 + 1; return true;
      case 'v': vel = vel >= 127 ? 40 : std::min(127, vel + 20); return true;
      case 'm': localSound = !localSound; toast(localSound ? "LOCAL SOUND ON" : "LOCAL SOUND OFF"); return true;
      case 'b': if (BleMidi::on) { BleMidi::end(); toast("BLUETOOTH OFF"); } else { BleMidi::begin(); if (BleMidi::on) toast("BLUETOOTH MIDI ON"); } return true;
      case '5': chord = (chord + 1) % 4; return true;
      case '[': cc = (cc + 3) % 4; return true;
      case ']': cc = (cc + 1) % 4; return true;
      case '-': case '=': case '_': case '+': {
        int d = (c == '-' || c == '_') ? -8 : 8;
        ccVal[cc] = constrain(ccVal[cc] + d, 0, 127);
        midiOut(0xB0 | (cfg.midiCh - 1), ccNum[cc], ccVal[cc]);
        snprintf(b, sizeof(b), "CC%d = %d", ccNum[cc], ccVal[cc]); logMsg(">", b);
        return true;
      }
      case '9': case '0':
        program = constrain(program + (c == '0' ? 1 : -1), 0, 127);
        midiOut(0xC0 | (cfg.midiCh - 1), program, 0);
        snprintf(b, sizeof(b), "PROGRAM %d", program); logMsg(">", b);
        return true;
      case ' ':
        for (int n = 0; n < 128; n++) midiOut(0x80 | (cfg.midiCh - 1), n, 0);
        Audio::allOff(); logMsg(">", "ALL NOTES OFF");
        return true;
    }
    return false;
  }
  void tick() override {
    if (page != LIVE) return;
    std::vector<char> now;
    if (!heldFn) for (char k : heldKeys) if (semitone(k) >= 0) now.push_back(k);
    for (int i = held.size() - 1; i >= 0; i--)
      if (std::find(now.begin(), now.end(), held[i].c) == now.end()) {
        for (auto n : held[i].notes) {
          midiOut(0x80 | (cfg.midiCh - 1), n, 0);
          if (localSound) Audio::noteOff(n, 0);
        }
        char b[24]; snprintf(b, sizeof(b), "OFF %s", noteName(held[i].notes[0])); logMsg(">", b);
        held.erase(held.begin() + i);
      }
    static const int8_t CH[4][4] = {{0, -1, -1, -1}, {0, 4, 7, -1}, {0, 3, 7, -1}, {0, 4, 7, 10}};
    for (char k : now) {
      bool was = false;
      for (auto& h : held) if (h.c == k) was = true;
      if (was) continue;
      Held h{k, {}};
      int base = 12 * (octave + 1) + semitone(k);
      for (int i = 0; i < 4 && CH[chord][i] >= 0; i++) {
        uint8_t n = constrain(base + CH[chord][i], 0, 127);
        midiOut(0x90 | (cfg.midiCh - 1), n, vel);
        if (localSound) Audio::noteOn(n, Looper::tr[Looper::recTrack].wave, 0);
        h.notes.push_back(n);
      }
      char b[24]; snprintf(b, sizeof(b), "ON  %-4s V%d", noteName(h.notes[0]), vel); logMsg(">", b);
      held.push_back(h);
    }
  }
  void drawStatus(int x, int y) {
#if HAS_USB_MIDI
    bool usb = (bool)USB;
#else
    bool usb = false;
#endif
    text(x, y, usb ? "USB" : "USB", usb ? T.green : T.dim, 1, false);
    const char* bt = !BleMidi::on ? "BT OFF" : (BleMidi::connected ? "BT LINK" : "BT WAIT");
    text(x + 24, y, bt, !BleMidi::on ? T.dim : (BleMidi::connected ? T.green : T.yel), 1, false);
    cv.fillCircle(x + 74, y + 3, 3, millis() - rxFlash < 120 ? T.green : T.dim);
    cv.fillCircle(x + 84, y + 3, 3, millis() - txFlash < 120 ? T.pink : T.dim);
  }
  void drawLive() {
    panel(3, 14, 100, 62, T.edge);
    drawIcon(8, 18, IC_MIDI, 2, T.pink);
    char b[32];
    snprintf(b, sizeof(b), "CH %d", cfg.midiCh); text(46, 20, b, T.white, 1, false);
    snprintf(b, sizeof(b), "OCT %d", octave); text(46, 31, b, T.white, 1, false);
    snprintf(b, sizeof(b), "VEL %d", vel); text(46, 42, b, T.white, 1, false);
    if (chord) text(46, 53, CHORD[chord], T.yel, 1, false);
    text(76, 53, localSound ? "LOCAL" : "", T.cyan, 1, false);
    drawStatus(8, 64);
    panel(3, 78, 100, 45, T.edge);
    text(8, 82, ccName[cc], T.pink, 1, false);
    snprintf(b, sizeof(b), "CC%d", ccNum[cc]); text(70, 82, b, T.dim, 1, false);
    int kx = 30, ky = 106;
    for (int k = 0; k < 40; k++) {
      float ang = PI * 0.75f + k / 39.f * PI * 1.5f;
      cv.drawPixel(kx + 12 * cosf(ang), ky + 12 * sinf(ang), k / 39.f <= ccVal[cc] / 127.f ? gradc(k / 39.f) : T.dim);
    }
    float ang = PI * 0.75f + ccVal[cc] / 127.f * PI * 1.5f;
    cv.drawLine(kx, ky, kx + 8 * cosf(ang), ky + 8 * sinf(ang), T.white);
    snprintf(b, sizeof(b), "%d", ccVal[cc]); text(56, 100, b, T.white, 2);
    panel(107, 14, 130, 109, T.edge, T.bg0);
    text(112, 18, "MIDI LOG", T.grey, 1, false);
    for (int i = 0; i < logN && i < 7; i++) text(112, 30 + i * 12, logLines[i].c_str(), i == 0 ? T.white : (logLines[i][0] == '<' ? T.green : T.dim), 1, false);
    if (!logN) { text(112, 40, "PLAY A-' KEYS", T.dim, 1, false); text(112, 52, "B = BLUETOOTH MIDI", T.dim, 1, false); text(112, 64, "USB: PLUG INTO PC", T.dim, 1, false); }
    hintBar({{"A-'", "PLAY"}, {"ZX", "OCT"}, {"B", "BT"}, {"5", "CHORD"}, {"TAB", "PLAYER"}});
  }
  void drawPlayer() {
    panel(3, 14, 234, 76, T.edge);
    if (files.empty()) {
      textC(120, 36, "NO MIDI FILES", T.white);
      textC(120, 50, "PUT .MID FILES IN /MIDI", T.grey, 1, false);
      textC(120, 62, "(SYNTH FN+E EXPORTS ONE)", T.dim, 1, false);
    }
    int top = std::max(0, fsel - 5);
    for (int r = 0; r < 6 && top + r < (int)files.size(); r++) {
      int i = top + r, y = 18 + r * 11;
      bool s = i == fsel;
      bool pl = MidiPlay::playing && loaded == fullPath(i);
      if (s) { cv.fillRect(5, y - 1, 230, 11, lerpc(T.panel, T.pink, 0.2f)); cv.fillRect(5, y - 1, 2, 11, T.pink); }
      text(10, y + 1, pl ? ">" : " ", T.pink, 1, false);
      textFit(18, y + 1, files[i].c_str(), s ? T.white : T.grey, 210, false);
    }
    panel(3, 92, 234, 31, T.edge, T.bg0);
    float L = MidiPlay::lenSec(), P = MidiPlay::posSec();
    if (loaded.length()) {
      textFit(8, 96, baseName(loaded).c_str(), MidiPlay::playing ? T.white : T.grey, 150, false);
      char b[24];
      snprintf(b, sizeof(b), "%u EVENTS", (unsigned)MidiPlay::events());
      text(232 - textW(b), 96, b, T.dim, 1, false);
      int pw = 224, pp = L > 0 ? constrain((int)(pw * P / L), 0, pw) : 0;
      cv.drawFastHLine(8, 112, pw, T.dim);
      for (int x = 0; x < pp; x++) cv.drawPixel(8 + x, 112, gradc(x / (float)pw));
      text(8, 115, fmtTime((uint32_t)P).c_str(), T.grey, 1, false);
    } else text(8, 100, "OK = PLAY THROUGH THE SYNTH", T.dim, 1, false);
    drawStatus(140, 115);
    hintBar({{"^v", "FILE"}, {"OK", "PLAY/STOP"}, {"B", "BT"}, {"TAB", "LIVE"}});
  }
  void draw() override {
    background();
    statusBar(page == LIVE ? "MIDI LIVE" : "MIDI PLAYER", T.pink);
    for (int i = 0; i < NPAGE; i++) cv.fillRect(110 + i * 10, 13, 8, 1, i == page ? T.pink : T.dim);
    if (page == LIVE) drawLive();
    else drawPlayer();
  }
};

App* appMidi = new MidiApp();
