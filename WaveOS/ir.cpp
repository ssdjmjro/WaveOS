// WAVE OS - IR remote: built-in TV codes + learn mode via Grove IR receiver
#define SEND_PWM_BY_TIMER
#define NO_LED_FEEDBACK_CODE
#include <IRremote.hpp>
#include "common.h"
#include "audio.h"
#include <functional>
#include <algorithm>

static constexpr int IR_TX = 44;
static constexpr int NB = 12;
static const char* BTN[NB] = {"POWER", "INPUT", "MUTE", "VOL+", "UP", "CH+", "LEFT", "OK", "RIGHT", "VOL-", "DOWN", "CH-"};

struct Brand { const char* n; uint8_t proto; uint16_t addr; uint8_t cmd[NB]; bool beta; };
enum { P_SAMSUNG, P_NEC, P_SONY, P_RC5, P_PANA, P_SHARP, P_JVC };
#define NA 0xFF  // button not available on this brand
//                                              POWER INPUT MUTE VOL+  UP   CH+  LEFT  OK  RIGHT VOL- DOWN CH-
static const Brand BRANDS[] = {
    {"SAMSUNG", P_SAMSUNG, 0x0707, {0x02, 0x01, 0x0F, 0x07, 0x60, 0x12, 0x65, 0x68, 0x62, 0x0B, 0x61, 0x10}, false},
    {"LG", P_NEC, 0x04, {0x08, 0x0B, 0x09, 0x02, 0x40, 0x00, 0x07, 0x44, 0x06, 0x03, 0x41, 0x01}, false},
    {"SONY", P_SONY, 0x01, {21, 37, 20, 18, 116, 16, 52, 101, 51, 19, 117, 17}, false},
    {"PHILIPS", P_RC5, 0x00, {12, 56, 13, 16, 80, 32, 85, 87, 86, 17, 81, 33}, false},
    {"PANASONIC", P_PANA, 0x08, {0x3D, 0x05, 0x32, 0x20, 0x4A, 0x34, 0x4E, 0x49, 0x4F, 0x21, 0x4B, 0x35}, true},
    {"TOSHIBA", P_NEC, 0x40, {0x12, 0x0F, 0x10, 0x1A, NA, 0x1B, NA, NA, NA, 0x1E, NA, 0x1F}, true},
    {"SHARP", P_SHARP, 0x01, {0x16, 0x13, 0x17, 0x14, NA, 0x11, NA, NA, NA, 0x15, NA, 0x12}, true},
    {"JVC", P_JVC, 0x03, {0x17, 0x13, 0x1C, 0x1E, NA, 0x19, NA, NA, NA, 0x1F, NA, 0x18}, true},
    {"VIZIO", P_NEC, 0x04, {0x08, 0x2F, 0x09, 0x02, NA, 0x00, NA, NA, NA, 0x03, NA, 0x01}, true},
};
static constexpr int NBR = sizeof(BRANDS) / sizeof(BRANDS[0]);

struct Learned { bool set; IRData d; std::vector<uint16_t> raw; };  // raw (us) for remotes with unknown protocols
static Learned learned[NB];
static bool learning = false;
static int learnSlot = -1;
static uint32_t learnUntil = 0;
static String lastInfo;
static uint32_t sentT = 0;
static bool txReady = false;

static void loadLearned() {
  for (auto& l : learned) { l.set = false; memset(&l.d, 0, sizeof(IRData)); l.raw.clear(); }
  File f = SD.open("/ir/learned.txt");
  if (!f) return;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    int s, p, a, c, b;
    if (sscanf(l.c_str(), "%d %d %x %x %d", &s, &p, &a, &c, &b) == 5 && s >= 0 && s < NB) {
      learned[s].set = true;
      learned[s].d.protocol = (decode_type_t)p;
      learned[s].d.address = a;
      learned[s].d.command = c;
      learned[s].d.numberOfBits = b;
      int r = l.indexOf(" raw ");  // "slot 0 0 0 0 raw 9000 4500 560 ..."
      if (r > 0) {
        const char* q = l.c_str() + r + 5;
        while (*q && learned[s].raw.size() < 400) {
          char* e; long v = strtol(q, &e, 10);
          if (e == q) break;
          learned[s].raw.push_back((uint16_t)constrain(v, 1L, 65000L));
          q = e;
        }
      }
    }
  }
  f.close();
}
static void saveLearned() {
  if (!sdOK) return;
  File f = SD.open("/ir/learned.txt", FILE_WRITE);
  if (!f) return;
  for (int i = 0; i < NB; i++) {
    if (!learned[i].set) continue;
    f.printf("%d %d %x %x %d", i, (int)learned[i].d.protocol, learned[i].d.address, learned[i].d.command, learned[i].d.numberOfBits);
    if (learned[i].raw.size()) { f.print(" raw"); for (auto t : learned[i].raw) f.printf(" %u", t); }
    f.print("\n");
  }
  f.close();
}

// ---- send helper shared by the remote UI and macros
static String sendCode(int brand, int b) {
  char info[48] = "";
  if (brand == NBR) {
    if (!learned[b].set) return "";
    if (learned[b].raw.size()) {
      IrSender.sendRaw(learned[b].raw.data(), learned[b].raw.size(), 38);
      snprintf(info, sizeof(info), "RAW %d TIMINGS", (int)learned[b].raw.size());
    } else {
      IrSender.write(&learned[b].d, 1);
      snprintf(info, sizeof(info), "%s A%X C%X", getProtocolString(learned[b].d.protocol), learned[b].d.address, learned[b].d.command);
    }
  } else {
    const Brand& br = BRANDS[brand];
    uint8_t c = br.cmd[b];
    if (c == NA) return "";
    switch (br.proto) {
      case P_SHARP: IrSender.sendSharp((uint8_t)br.addr, c, 1); break;
      case P_JVC: IrSender.sendJVC((uint8_t)br.addr, c, 1); break;
      case P_SAMSUNG: IrSender.sendSamsung(br.addr, c, 1); break;
      case P_NEC: IrSender.sendNEC(br.addr, c, 1); break;
      case P_SONY: IrSender.sendSony(br.addr, c, 2, 12); break;
      case P_RC5: IrSender.sendRC5(br.addr, c, 1, true); break;
      case P_PANA: IrSender.sendPanasonic(br.addr, c, 1); break;
    }
    static const char* pn[] = {"SAMSUNG", "NEC", "SONY12", "RC5", "PANASONIC", "SHARP", "JVC"};
    snprintf(info, sizeof(info), "%s A%X C%X", pn[br.proto], br.addr, c);
  }
  lastInfo = info;
  sentT = millis();
  return String(info);
}

// ---- receiver on the Grove port (G1 or G2). Both pins are watched, so if the receiver's signal
// wire is on the other pin we switch to it automatically.
static volatile uint32_t edgeN[3] = {0, 0, 0}, edgeT[3] = {0, 0, 0};
static void IRAM_ATTR isrG1() { edgeN[1]++; edgeT[1] = millis(); }
static void IRAM_ATTR isrG2() { edgeN[2]++; edgeT[2] = millis(); }
static bool rxOn = false;
static uint32_t rxIgnoreUntil = 0, rxSeen[3] = {0, 0, 0};
static int rxPin() { return cfg.irRx == 2 ? 2 : 1; }
static void rxPins() {  // active pin: pull-up (works with open-collector / diode wiring); other pin: pull-down so it can't float
  pinMode(rxPin(), INPUT_PULLUP);
  pinMode(3 - rxPin(), INPUT_PULLDOWN);
}
static void rxStart() {
  if (rxOn) return;
  IrReceiver.begin(rxPin(), false);
  rxPins();
  attachInterrupt(1, isrG1, CHANGE);
  attachInterrupt(2, isrG2, CHANGE);
  edgeN[1] = edgeN[2] = 0; rxSeen[1] = rxSeen[2] = 0;
  rxOn = true;
}
static void rxStop() {
  if (!rxOn) return;
  detachInterrupt(1); detachInterrupt(2);
  IrReceiver.stop();
  pinMode(1, INPUT); pinMode(2, INPUT);
  rxOn = false;
}
static void rxSwitch(int pin) {
  if (pin == rxPin()) return;
  cfg.irRx = pin;
  saveSettings();
  IrReceiver.stop();
  IrReceiver.begin(rxPin(), false);
  rxPins();
  edgeN[1] = edgeN[2] = 0; rxSeen[1] = rxSeen[2] = 0;
}
// a burst of edges on the inactive pin while the active one is silent -> the receiver is on the other pin
static void rxAutoPin() {
  uint32_t now = millis();
  int a = rxPin(), o = 3 - a;
  uint32_t na = edgeN[a], no = edgeN[o];
  if (no - rxSeen[o] >= 12 && na == rxSeen[a] && now - edgeT[o] < 400) {
    rxSwitch(o);
    char b[40]; snprintf(b, sizeof(b), "IR RECEIVER FOUND ON G%d", o); toast(b, 1600);
    return;
  }
  if (now - edgeT[o] > 1500) rxSeen[o] = no;  // forget old noise
  rxSeen[a] = na;
}
// copy the last frame as microseconds (marks/spaces), for remotes IRremote can't decode
static void rxRaw(std::vector<uint16_t>& out) {
  out.clear();
  auto& ip = IrReceiver.irparams;
  int n = std::min<int>(ip.rawlen, 401);
  for (int i = 1; i < n; i++) {
    int us = ip.rawbuf[i] * MICROS_PER_TICK;
    us += (i & 1) ? -MARK_EXCESS_MICROS : MARK_EXCESS_MICROS;
    out.push_back((uint16_t)constrain(us, 1, 65000));
  }
  if (!(out.size() & 1) && out.size()) out.pop_back();  // must end on a mark
}
static String irDesc(const IRData& d, int rawN) {
  char b[48];
  if (d.protocol == UNKNOWN) snprintf(b, sizeof(b), "UNKNOWN  RAW %d", rawN);
  else snprintf(b, sizeof(b), "%s A%X C%X %dB", getProtocolString(d.protocol), d.address, d.command, d.numberOfBits);
  return String(b);
}

// RECEIVE screen: live capture list
struct Capture { IRData d; std::vector<uint16_t> raw; uint32_t t; int reps; };
static std::vector<Capture> caps;
static bool scanning = false;

// ---- macros: /ir/macros.txt   NAME: SAMSUNG POWER, WAIT 2000, LG VOL+ x5
struct Macro { String name; std::vector<String> steps; };
static std::vector<Macro> macros;
static int runMacro = -1, runStep = 0, runRep = 0;
static uint32_t runAt = 0;
static void loadMacros() {
  macros.clear();
  if (sdOK && !SD.exists("/ir/macros.txt")) {
    File f = SD.open("/ir/macros.txt", FILE_WRITE);
    if (f) {
      f.println("# WAVE OS IR macros (one per line)");
      f.println("# NAME: BRAND BUTTON [xN], WAIT ms, ...");
      f.println("# brands: SAMSUNG LG SONY PHILIPS PANASONIC LEARNED");
      f.println("# buttons: POWER INPUT MUTE VOL+ VOL- CH+ CH- UP DOWN LEFT RIGHT OK");
      f.println("ALL TV OFF: SAMSUNG POWER, WAIT 400, LG POWER, WAIT 400, SONY POWER, WAIT 400, PHILIPS POWER, WAIT 400, PANASONIC POWER");
      f.println("SAMSUNG HDMI: SAMSUNG POWER, WAIT 3000, SAMSUNG INPUT");
      f.println("LG LOUDER: LG VOL+ x5");
      f.println("LG QUIET: LG VOL- x5");
      f.close();
    }
  }
  File f = SD.open("/ir/macros.txt");
  if (!f) return;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (!l.length() || l.startsWith("#")) continue;
    int c = l.indexOf(':');
    if (c <= 0) continue;
    Macro m; m.name = l.substring(0, c); m.name.trim(); m.name.toUpperCase();
    String rest = l.substring(c + 1);
    while (rest.length()) {
      int k = rest.indexOf(',');
      String st = k < 0 ? rest : rest.substring(0, k);
      st.trim(); st.toUpperCase();
      if (st.length()) m.steps.push_back(st);
      if (k < 0) break;
      rest = rest.substring(k + 1);
    }
    macros.push_back(m);
  }
  f.close();
}
static void macroStep() {
  if (runMacro < 0 || millis() < runAt) return;
  Macro& m = macros[runMacro];
  if (runStep >= (int)m.steps.size()) { runMacro = -1; toast("MACRO DONE"); return; }
  String st = m.steps[runStep];
  if (st.startsWith("WAIT")) { runAt = millis() + st.substring(4).toInt(); runStep++; return; }
  int sp = st.indexOf(' ');
  String br = sp > 0 ? st.substring(0, sp) : st, rest = sp > 0 ? st.substring(sp + 1) : "";
  int times = 1;
  int x = rest.indexOf(" X");
  if (x > 0) { times = rest.substring(x + 2).toInt(); rest = rest.substring(0, x); }
  rest.trim();
  int bi = -1, bt = -1;
  if (br == "LEARNED") bi = NBR;
  for (int i = 0; i < NBR; i++) if (br == BRANDS[i].n) bi = i;
  for (int i = 0; i < NB; i++) if (rest == BTN[i]) bt = i;
  if (bi >= 0 && bt >= 0) sendCode(bi, bt);
  if (++runRep >= std::max(1, times)) { runRep = 0; runStep++; }
  runAt = millis() + 180;
}

// called every frame from loop(): learn mode + receive screen
void irTick() {
  macroStep();
  if (!learning && !scanning) return;
  rxAutoPin();
  if (learning && millis() > learnUntil) { learning = false; if (!scanning) rxStop(); toast("LEARN TIMED OUT - TRY IR > RECEIVE", 1800); return; }
  if (!IrReceiver.decode()) return;
  IRData d = IrReceiver.decodedIRData;
  std::vector<uint16_t> raw;
  rxRaw(raw);
  IrReceiver.resume();
  if (millis() < rxIgnoreUntil) return;  // our own IR LED
  bool rep = d.flags & IRDATA_FLAGS_IS_REPEAT;
  if (d.protocol == UNKNOWN && raw.size() < 7) return;  // noise
  if (learning) {
    if (rep) return;
    Learned& L = learned[learnSlot];
    L.set = true; L.d = d; L.d.flags = 0;
    if (d.protocol == UNKNOWN) L.raw = raw; else L.raw.clear();
    saveLearned();
    lastInfo = irDesc(d, raw.size());
    learning = false;
    if (!scanning) rxStop();
    Audio::blip(1500, 60, Audio::W_SINE);
    toast("LEARNED!");
    return;
  }
  // receive screen: repeats of the same button just bump a counter
  if (caps.size() && millis() - caps[0].t < 600 &&
      (rep || (d.protocol == caps[0].d.protocol && d.address == caps[0].d.address && d.command == caps[0].d.command && d.protocol != UNKNOWN))) {
    caps[0].reps++; caps[0].t = millis(); return;
  }
  if (rep) return;
  caps.insert(caps.begin(), Capture{d, raw, millis(), 1});
  if (caps.size() > 6) caps.pop_back();
  Audio::blip(1700, 25, Audio::W_SINE, 0.3f);
}

static bool saveCapture(const Capture& c) {  // append to /ir/captured.ir (Flipper format) -> shows up in .IR FILES
  if (!sdOK) return false;
  File f = SD.open("/ir/captured.ir", SD.exists("/ir/captured.ir") ? FILE_APPEND : FILE_WRITE);
  if (!f) return false;
  if (f.size() == 0) f.print("Filetype: IR signals file\nVersion: 1\n");
  static int n = 0;
  if (!n) n = millis() % 900 + 1;
  f.printf("#\nname: CAP_%d\n", n++);
  const IRData& d = c.d;
  const char* fp = nullptr;
  uint32_t a = d.address, cm = d.command;
  switch (d.protocol) {
    case NEC: fp = a > 0xFF ? "NECext" : "NEC"; break;
    case SAMSUNG: fp = "Samsung32"; a &= 0xFF; break;
    case SONY: fp = d.numberOfBits >= 20 ? "SIRC20" : d.numberOfBits >= 15 ? "SIRC15" : "SIRC"; break;
    case RC5: fp = "RC5"; break;
    case RC6: fp = "RC6"; break;
    default: break;
  }
  if (fp && strcmp(fp, "NECext") == 0) cm = (cm & 0xFF) | ((~cm & 0xFF) << 8);
  if (fp) {
    f.printf("type: parsed\nprotocol: %s\naddress: %02X %02X 00 00\ncommand: %02X %02X 00 00\n", fp, (unsigned)(a & 0xFF), (unsigned)(a >> 8 & 0xFF),
             (unsigned)(cm & 0xFF), (unsigned)(cm >> 8 & 0xFF));
  } else {
    // unknown, or a protocol the Flipper format has no name for: store the timings
    f.print("type: raw\nfrequency: 38000\nduty_cycle: 0.330000\ndata:");
    for (auto t : c.raw) f.printf(" %u", t);
    f.print("\n");
  }
  f.close();
  return true;
}
static void sendCapture(const Capture& c) {
  rxIgnoreUntil = millis() + 250;
  if (c.d.protocol == UNKNOWN) IrSender.sendRaw(c.raw.data(), c.raw.size(), 38);
  else IrSender.write(const_cast<IRData*>(&c.d), 0);
  lastInfo = irDesc(c.d, c.raw.size());
  sentT = millis();
}

// ---- Flipper-style .ir files in /ir (e.g. from the open Flipper-IRDB collection)
struct IrSig { String name, type, proto; uint32_t addr = 0, cmd = 0, freq = 38000; uint32_t dataPos = 0; };
static std::vector<String> irFiles;
static std::vector<IrSig> irSigs;
static String irFileOpen;
static void scanIrFiles() {
  irFiles.clear();
  std::function<void(const String&, int)> walk = [&](const String& dir, int depth) {
    File d = SD.open(dir);
    if (!d) return;
    while (irFiles.size() < 300) {
      File f = d.openNextFile();
      if (!f) break;
      String n = f.name(), full = dir + "/" + n, l = n;
      l.toLowerCase();
      bool isDir = f.isDirectory();
      f.close();
      if (isDir && depth > 0) walk(full, depth - 1);
      else if (l.endsWith(".ir")) irFiles.push_back(full.substring(4));  // strip "/ir/"
    }
    d.close();
  };
  walk("/ir", 2);
  std::sort(irFiles.begin(), irFiles.end());
}
static uint32_t hexBytesLE(const String& v) {  // "04 00 00 00" -> 0x00000004
  uint32_t r = 0; int sh = 0;
  for (int i = 0; i < (int)v.length() && sh < 32;) {
    while (i < (int)v.length() && v[i] == ' ') i++;
    if (i + 1 >= (int)v.length() + 1) break;
    String b = v.substring(i, i + 2);
    if (b.length() < 2) break;
    r |= strtoul(b.c_str(), nullptr, 16) << sh;
    sh += 8; i += 2;
  }
  return r;
}
static bool openIrFile(const String& rel) {
  irSigs.clear();
  File f = SD.open("/ir/" + rel);
  if (!f) return false;
  IrSig cur; bool have = false;
  while (f.available() && irSigs.size() < 80) {
    uint32_t lineStart = f.position();
    String l = f.readStringUntil('\n'); l.trim();
    int c = l.indexOf(':');
    if (c < 0) continue;
    String k = l.substring(0, c), v = l.substring(c + 1); v.trim();
    if (k == "name") { if (have) irSigs.push_back(cur); cur = IrSig(); cur.name = v; cur.name.toUpperCase(); have = true; }
    else if (k == "type") cur.type = v;
    else if (k == "protocol") cur.proto = v;
    else if (k == "address") cur.addr = hexBytesLE(v);
    else if (k == "command") cur.cmd = hexBytesLE(v);
    else if (k == "frequency") cur.freq = v.toInt();
    else if (k == "data") cur.dataPos = lineStart;
  }
  if (have) irSigs.push_back(cur);
  f.close();
  irFileOpen = rel;
  return true;
}
static String sendIrSig(const IrSig& s) {
  String p = s.proto;
  uint32_t a = s.addr, c = s.cmd;
  if (s.type == "raw") {
    File f = SD.open("/ir/" + irFileOpen);
    if (!f) return "";
    f.seek(s.dataPos);
    String l = f.readStringUntil('\n');
    f.close();
    static uint16_t buf[1024];
    int n = 0, i = l.indexOf(':') + 1;
    while (i > 0 && i < (int)l.length() && n < 1024) {
      while (i < (int)l.length() && l[i] == ' ') i++;
      long v = 0; bool any = false;
      while (i < (int)l.length() && isdigit((unsigned char)l[i])) { v = v * 10 + (l[i++] - '0'); any = true; }
      if (!any) break;
      buf[n++] = (uint16_t)std::min<long>(v, 65000);
    }
    if (!n) return "";
    IrSender.sendRaw(buf, n, (s.freq + 500) / 1000);
    return "RAW " + String(n) + " @" + String(s.freq / 1000) + "K";
  }
  if (p == "NEC" || p == "Pioneer") IrSender.sendNEC(a & 0xFF, c & 0xFF, 0);
  else if (p == "NECext") IrSender.sendNECRaw((a & 0xFFFF) | ((c & 0xFFFF) << 16), 0);
  else if (p == "Samsung32") IrSender.sendSamsung((a & 0xFF) | ((a & 0xFF) << 8), c & 0xFF, 0);
  else if (p == "SIRC") IrSender.sendSony(a, c, 2, 12);
  else if (p == "SIRC15") IrSender.sendSony(a, c, 2, 15);
  else if (p == "SIRC20") IrSender.sendSony(a, c, 2, 20);
  else if (p == "RC5" || p == "RC5X") IrSender.sendRC5(a, c + (p == "RC5X" ? 64 : 0), 0, true);
  else if (p == "RC6") IrSender.sendRC6(a, c, 0, true);
  else if (p == "RCA") {  // 56kHz: 4000/4000 header, 4-bit addr + 8-bit cmd, then their complements, MSB first
    static uint16_t rb[60];
    int n = 0;
    rb[n++] = 4000; rb[n++] = 4000;
    uint32_t bits = ((a & 0xF) << 8 | (c & 0xFF));
    uint32_t word = bits << 12 | (~bits & 0xFFF);
    for (int i = 23; i >= 0; i--) { rb[n++] = 500; rb[n++] = (word >> i & 1) ? 2000 : 1000; }
    rb[n++] = 500;
    IrSender.sendRaw(rb, n, 56);
  }
  else return "";
  char b[40]; snprintf(b, sizeof(b), "%s A%X C%X", p.c_str(), (unsigned)a, (unsigned)c);
  return String(b);
}

struct IRApp : App {
  int brand = 0;   // NBR = learned remote
  int btn = 0;
  bool inRemote = false;
  IRApp() { name = "IR"; }
  void enter() override {
    if (!txReady) { IrSender.begin(IR_TX); txReady = true; loadLearned(); }
    loadMacros();
  }
  void leave() override {
    learning = false; scanning = false; rxStop();
    caps.clear(); caps.shrink_to_fit();
    irSigs.clear(); irSigs.shrink_to_fit(); irFiles.clear(); irFiles.shrink_to_fit(); fileLevel = 0;
  }
  void send(int b) {
    if (brand == NBR && !learned[b].set) { toast("NOT LEARNED - PRESS L"); return; }
    sendCode(brand, b);
  }
  int msel = 0, fsel = 0, fileLevel = 0, fileSel0 = 0, csel = 0;
  bool key(const KeyEv& e) override {
    KeyCode k = e.nav();
    if (!inRemote) {
      if (k == K_UP) { brand = (brand + NBR + 3) % (NBR + 4); return true; }
      if (k == K_DOWN) { brand = (brand + 1) % (NBR + 4); return true; }
      if (k == K_ENTER || k == K_RIGHT) {
        inRemote = true; btn = 0; msel = 0; fsel = 0; fileLevel = 0;
        if (brand == NBR + 2) scanIrFiles();
        if (brand == NBR + 3) { scanning = true; csel = 0; rxStart(); }
        return true;
      }
      return false;
    }
    if (brand == NBR + 3) {  // receive
      int n = caps.size();
      if (k == K_ESC || k == K_LEFT) { inRemote = false; scanning = false; if (!learning) rxStop(); return true; }
      if (k == K_UP && n) { csel = (csel + n - 1) % n; return true; }
      if (k == K_DOWN && n) { csel = (csel + 1) % n; return true; }
      if (csel >= n) csel = 0;
      if (k == K_ENTER && n) { sendCapture(caps[csel]); return true; }
      if (e.is('s') && n) { toast(saveCapture(caps[csel]) ? "SAVED TO /IR/CAPTURED.IR" : "SAVE FAILED"); return true; }
      if (e.is('g')) { rxSwitch(3 - rxPin()); char b[24]; snprintf(b, sizeof(b), "LISTENING ON G%d", rxPin()); toast(b); return true; }
      if (e.is('x')) { caps.clear(); return true; }
      return false;
    }
    if (brand == NBR + 2) {  // .ir files
      int n = fileLevel == 0 ? irFiles.size() : irSigs.size();
      if (k == K_ESC || k == K_LEFT) { if (fileLevel) { fileLevel = 0; fsel = fileSel0; } else inRemote = false; return true; }
      if (k == K_UP && n) { fsel = (fsel + n - 1) % n; return true; }
      if (k == K_DOWN && n) { fsel = (fsel + 1) % n; return true; }
      if (k == K_ENTER && n) {
        if (fileLevel == 0) { if (openIrFile(irFiles[fsel])) { fileSel0 = fsel; fileLevel = 1; fsel = 0; } else toast("CAN'T OPEN FILE"); }
        else { String r = sendIrSig(irSigs[fsel]); if (r.length()) { lastInfo = r; sentT = millis(); } else toast("PROTOCOL NOT SUPPORTED"); }
        return true;
      }
      return false;
    }
    if (brand == NBR + 1) {  // macros
      int n = macros.size();
      if (k == K_ESC || k == K_LEFT) { inRemote = false; return true; }
      if (k == K_UP && n) { msel = (msel + n - 1) % n; return true; }
      if (k == K_DOWN && n) { msel = (msel + 1) % n; return true; }
      if (k == K_ENTER && n) { runMacro = msel; runStep = 0; runRep = 0; runAt = 0; toast("RUNNING MACRO"); return true; }
      if (e.is('r')) { loadMacros(); toast("MACROS RELOADED"); return true; }
      if (e.code == K_CHAR && e.ch == ' ') { runMacro = -1; toast("MACRO STOPPED"); return true; }
      return false;
    }
    if (k == K_ESC) { inRemote = false; if (learning) { learning = false; rxStop(); } return true; }
    int r = btn / 3, c = btn % 3;
    if (k == K_UP) { btn = ((r + 3) % 4) * 3 + c; return true; }
    if (k == K_DOWN) { btn = ((r + 1) % 4) * 3 + c; return true; }
    if (k == K_LEFT) { btn = r * 3 + (c + 2) % 3; return true; }
    if (k == K_RIGHT) { btn = r * 3 + (c + 1) % 3; return true; }
    if (k == K_ENTER) { send(btn); return true; }
    if (e.code != K_CHAR) return false;
    switch (tolower(e.ch)) {  // quick keys
      case 'p': send(0); return true;
      case 'i': send(1); return true;
      case 'm': send(2); return true;
      case '=': case '+': send(3); return true;
      case '-': case '_': send(9); return true;
      case ']': send(5); return true;
      case '[': send(11); return true;
      case 'o': send(7); return true;
      case 'l':
        if (brand != NBR) { toast("SELECT 'LEARNED' REMOTE"); return true; }
        learnSlot = btn; learning = true; learnUntil = millis() + 15000;
        rxStart();
        return true;
      case 'x':
        if (brand == NBR) { learned[btn].set = false; saveLearned(); toast("BUTTON CLEARED"); }
        return true;
    }
    return false;
  }
  void draw() override {
    background();
    statusBar("IR REMOTE", T.red);
    panel(3, 15, 84, 108, T.edge);
    int total = NBR + 4, top = constrain(brand - 4, 0, std::max(0, total - 8));
    for (int r = 0; r < 8 && top + r < total; r++) {
      int i = top + r, y = 18 + r * 13;
      bool s = i == brand;
      if (s) { cv.fillRect(5, y - 2, 80, 12, lerpc(T.panel, T.red, 0.25f)); frame(5, y - 2, 80, 12, inRemote ? T.red : T.white); }
      const char* n = i == NBR ? "+ LEARNED" : i == NBR + 1 ? "> MACROS" : i == NBR + 2 ? "# .IR FILES" : i == NBR + 3 ? "~ RECEIVE" : BRANDS[i].n;
      text(9, y, n, s ? T.white : (i >= NBR ? (i == NBR ? T.green : i == NBR + 1 ? T.purp : i == NBR + 2 ? T.cyan : T.yel) : T.grey), 1, s);
      if (i < NBR && BRANDS[i].beta) cv.fillRect(80, y + 2, 2, 2, T.yel);
    }
    if (top > 0) cv.fillTriangle(42, 15, 46, 15, 44, 13, T.grey);
    if (top + 8 < total) cv.fillTriangle(42, 121, 46, 121, 44, 123, T.grey);
    if (brand == NBR + 3) {
      panel(91, 15, 146, 108, T.edge, T.bar);
      uint32_t now = millis();
      for (int p = 1; p <= 2; p++) {  // activity lamps for both Grove signal pins
        int x = 96 + (p - 1) * 46;
        bool act = rxOn && now - edgeT[p] < 120;
        cv.fillCircle(x + 3, 22, 3, act ? T.green : T.dim);
        char b[8]; snprintf(b, sizeof(b), "G%d", p);
        text(x + 9, 19, b, rxOn && p == rxPin() ? T.white : T.dim, 1, false);
      }
      if (rxOn) text(190, 19, "LIVE", (now / 400) % 2 ? T.yel : T.grey, 1, false);
      int n = caps.size();
      if (!n) {
        text(96, 36, rxOn ? "POINT A REMOTE HERE" : "OK = START LISTENING", T.white, 1, false);
        text(96, 48, "AND PRESS A BUTTON", T.white, 1, false);
        text(96, 64, "GROVE: RED 5V BLACK GND", T.dim, 1, false);
        text(96, 74, "SIGNAL ON G1 OR G2", T.dim, 1, false);
        text(96, 84, "(PIN FOUND BY ITSELF)", T.dim, 1, false);
      }
      for (int r = 0; r < n && r < 6; r++) {
        int y = 32 + r * 12;
        bool s = inRemote && r == csel;
        if (s) { cv.fillRect(93, y - 2, 142, 11, lerpc(T.panel, T.yel, 0.25f)); frame(93, y - 2, 142, 11, T.yel); }
        String l = irDesc(caps[r].d, caps[r].raw.size());
        if (caps[r].reps > 1) l += " x" + String(caps[r].reps);
        textFit(97, y, l.c_str(), s ? T.white : (r == 0 && now - caps[0].t < 300 ? T.yel : T.grey), 136, false);
      }
      if (lastInfo.length() && now - sentT < 1500) { text(96, 106, "TX )))", T.red, 1, false); textFit(134, 106, lastInfo.c_str(), T.grey, 100, false); }
      if (inRemote) hintBar({{"OK", "RESEND"}, {"S", "SAVE .IR"}, {"G", "PIN"}, {"X", "CLEAR"}});
      else hintBar({{"^v", "BRAND"}, {"OK", "OPEN"}, {"ESC", "BACK"}});
      return;
    }
    if (brand == NBR + 2) {
      panel(91, 15, 146, 108, T.edge, T.bar);
      bool lvl = fileLevel == 1;
      textFit(96, 19, lvl ? irFileOpen.c_str() : "/IR/*.IR (FLIPPER FORMAT)", T.grey, 136, false);
      int n = lvl ? irSigs.size() : irFiles.size();
      if (!n) { text(96, 40, lvl ? "NO SIGNALS" : "NO .IR FILES YET", T.white, 1, false); text(96, 52, "COPY FLIPPER-IRDB", T.dim, 1, false); text(96, 62, "FILES INTO /IR", T.dim, 1, false); }
      int tp = std::max(0, fsel - 5);
      for (int r = 0; r < 6 && tp + r < n; r++) {
        int i = tp + r, y = 31 + r * 11;
        bool s = inRemote && i == fsel;
        if (s) { cv.fillRect(93, y - 2, 142, 11, lerpc(T.panel, T.cyan, 0.25f)); frame(93, y - 2, 142, 11, T.cyan); }
        String lbl = lvl ? irSigs[i].name : irFiles[i];
        if (!lvl) { lbl.toUpperCase(); lbl.replace(".IR", ""); }
        textFit(98, y, lbl.c_str(), s ? T.white : T.grey, 132, false);
      }
      if (lastInfo.length() && lvl) { text(96, 100, millis() - sentT < 200 ? "TX )))" : "TX", T.red, 1, false); textFit(134, 100, lastInfo.c_str(), T.grey, 100, false); }
      if (inRemote) hintBar({{"^v", lvl ? "SIGNAL" : "FILE"}, {"OK", lvl ? "SEND" : "OPEN"}, {"ESC", "BACK"}});
      else hintBar({{"^v", "BRAND"}, {"OK", "OPEN"}, {"ESC", "BACK"}});
      return;
    }
    if (brand == NBR + 1) {
      panel(91, 15, 146, 108, T.edge, T.bar);
      text(96, 19, "MACROS /IR/MACROS.TXT", T.grey, 1, false);
      for (int i = 0; i < (int)macros.size() && i < 7; i++) {
        int y = 31 + i * 12;
        bool s = inRemote && i == msel;
        if (s) { cv.fillRect(93, y - 2, 142, 11, lerpc(T.panel, T.purp, 0.3f)); frame(93, y - 2, 142, 11, T.purp); }
        textFit(98, y, macros[i].name.c_str(), runMacro == i ? T.green : (s ? T.white : T.grey), 130, false);
      }
      if (runMacro >= 0) text(96, 112, "RUNNING...", T.green, 1, false);
      if (inRemote) hintBar({{"^v", "MACRO"}, {"OK", "RUN"}, {"SPC", "STOP"}, {"R", "RELOAD"}});
      else hintBar({{"^v", "BRAND"}, {"OK", "OPEN"}, {"ESC", "BACK"}});
      return;
    }
    // remote body
    panel(91, 15, 146, 108, T.edge, T.bar);
    char hdr[32];
    snprintf(hdr, sizeof(hdr), "%s%s", brand == NBR ? "LEARNED" : BRANDS[brand].n, brand < NBR && BRANDS[brand].beta ? " BETA" : "");
    text(96, 19, hdr, T.grey, 1, false);
    bool flash = millis() - sentT < 200;
    for (int i = 0; i < NB; i++) {
      int r = i / 3, c = i % 3;
      int x = 96 + c * 46, y = 30 + r * 17;
      bool s = inRemote && i == btn;
      bool dim = (brand == NBR && !learned[i].set) || (brand < NBR && BRANDS[brand].cmd[i] == NA);
      uint16_t edge = i == 0 ? T.red : (s ? T.cyan : T.edge);
      uint16_t fill = s ? lerpc(T.panel, flash ? T.white : T.cyan, flash ? 0.5f : 0.18f) : T.panel;
      panel(x, y, 44, 15, edge, fill);
      textC(x + 22, y + 4, BTN[i], dim ? T.dim : (s ? T.white : T.grey), 1, false);
    }
    if (learning) {
      int left = (learnUntil - millis()) / 1000 + 1;
      char b[40]; snprintf(b, sizeof(b), "PRESS REMOTE NOW %ds G%d", left, rxPin());
      text(96, 100, b, (millis() / 300) % 2 ? T.green : T.white, 1, false);
    } else if (lastInfo.length()) {
      text(96, 100, flash ? "TX )))" : "TX", T.red, 1, false);
      textFit(134, 100, lastInfo.c_str(), T.grey, 100, false);
    }
    text(96, 111, brand == NBR ? "L=LEARN X=CLEAR" : "P I M +- [] O", T.dim, 1, false);
    if (inRemote) hintBar({{"<>^v", "BUTTON"}, {"OK", "SEND"}, {"L", "LEARN"}, {"ESC", "BRANDS"}});
    else hintBar({{"^v", "BRAND"}, {"OK", "OPEN"}, {"ESC", "BACK"}});
  }
};

App* appIR = new IRApp();
