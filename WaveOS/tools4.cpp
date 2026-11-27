// WAVE OS - METER (in TOOLS): a simple multimeter on the Grove port or the 14-pin EXT header (dupont wires)
//   VOLTS  probe pin (+) to GND (-): 0 - 3.1 V direct, more through a resistor divider (x2, x11)
//   OHMS   drive pin -[reference resistor]- probe pin -[part]- GND, beeps for continuity
//   DIODE  same wiring, diode anode on the probe pin: forward voltage + what kind of diode
//   CAP    same wiring, capacitor instead of the part: charge time through the reference resistor
//   LOGIC  HIGH / LOW / FLOATING and the frequency of a signal on the probe pin
//   BATT   the Cardputer's own battery
// Probe / drive pins: Grove G1 G2, EXT G3 G4 G5 G6 G13 G15 (all have an ADC). Not offered: G8 G9 (the
// keyboard / IMU / audio I2C bus) and G14 G39 G40 (the SD card bus). GND is EXT pin 4 or the black Grove wire.
// The ESP32 pins take 3.3 V at most: 5VIN / 5VOUT / the red Grove wire are 5 V, never put them on a probe.
#include "common.h"
#include "audio.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/pulse_cnt.h"

struct MPin { uint8_t gpio; const char* where; };
static const MPin MPINS[] = {{1, "GROVE"}, {2, "GROVE"}, {3, "EXT 1"}, {4, "EXT 3"}, {6, "EXT 5"},
                             {5, "EXT 13"}, {13, "EXT 12"}, {15, "EXT 14"}};
static constexpr int NMP = sizeof(MPINS) / sizeof(MPINS[0]);
static int M_PIN = 1, M_REF = 2;  // current probe / drive GPIO
// LOGIC counts edges with the hardware pulse counter (an interrupt per edge froze the device when a loose
// wire picked up noise). The counter wraps at 32767, so it is read every tick (50 ms) and added up.
static pcnt_unit_handle_t pcU = nullptr;
static pcnt_channel_handle_t pcC = nullptr;
static uint32_t pcTotal = 0;
static void pcntStop() {
  if (!pcU) return;
  pcnt_unit_stop(pcU); pcnt_unit_disable(pcU);
  if (pcC) pcnt_del_channel(pcC);
  pcnt_del_unit(pcU);
  pcU = nullptr; pcC = nullptr;
}
static bool pcntStart(int gpio) {
  pcntStop();
  pcnt_unit_config_t uc = {}; uc.low_limit = -1; uc.high_limit = 32767;
  if (pcnt_new_unit(&uc, &pcU) != ESP_OK) { pcU = nullptr; return false; }
  pcnt_glitch_filter_config_t fc = {}; fc.max_glitch_ns = 1000;  // ignore spikes under 1 us
  pcnt_unit_set_glitch_filter(pcU, &fc);
  pcnt_chan_config_t cc = {}; cc.edge_gpio_num = gpio; cc.level_gpio_num = -1;
  if (pcnt_new_channel(pcU, &cc, &pcC) != ESP_OK) { pcC = nullptr; pcntStop(); return false; }
  pcnt_channel_set_edge_action(pcC, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
  gpio_set_pull_mode((gpio_num_t)gpio, GPIO_FLOATING);  // (the driver turns the pull-up on)
  pcnt_unit_enable(pcU); pcnt_unit_clear_count(pcU); pcnt_unit_start(pcU);
  pcTotal = 0;
  return true;
}
static void pcntPoll() {
  if (!pcU) return;
  int v = 0; pcnt_unit_get_count(pcU, &v); pcnt_unit_clear_count(pcU);
  if (v > 0) pcTotal += v;
}
static constexpr float VCC = 3300;  // drive pin HIGH (mV)

struct MeterApp : App {
  enum { M_VOLT, M_OHM, M_DIODE, M_CAP, M_LOGIC, M_BATT, M_N };
  int mode = M_VOLT, div = 0, ref = 1, probe = 0, drive = 1;  // probe / drive = index into MPINS
  bool hold = false, beep = true, help = false, over = false, open = false, pinMap = false;
  bool guide = false; uint8_t seen = 0;      // HOW-TO page, shown the first time each mode is opened
  bool rel = false; float relV = 0;           // REL: show the difference to a stored reading
  float val = 0, vmin = 0, vmax = 0, vsum = 0;
  uint32_t vn = 0, lastT = 0, beepT = 0, winT = 0, winE = 0;
  float trace[200] = {}; int tn = 0;
  float hz = 0;
  int logic = 0;            // 0 LOW, 1 HIGH, 2 FLOATING, 3 TOGGLING
  const char* note = "";    // short advice under the reading
  static constexpr float DIVS[3] = {1, 2, 11};
  static constexpr int REFS[3] = {1000, 10000, 100000};
  MeterApp() { name = "METER"; }
  const char* saveKey() override { return "meter"; }
  void persist(Persist& p) override {
    p.io(mode); p.io(div); p.io(ref); p.io(probe); p.io(drive); p.io(beep); p.io(seen);
    if (p.loading) { mode %= M_N; div %= 3; ref %= 3; probe %= NMP; drive %= NMP; if (drive == probe) drive = (probe + 1) % NMP; }
  }
  bool usesDrive() const { return mode == M_OHM || mode == M_DIODE || mode == M_CAP; }

  void pinsOff() { pcntStop(); pinMode(M_PIN, INPUT); pinMode(M_REF, INPUT); }
  void setup() {
    pinsOff();
    M_PIN = MPINS[probe].gpio; M_REF = MPINS[drive].gpio;
    if (mode == M_OHM || mode == M_DIODE) { pinMode(M_REF, OUTPUT); digitalWrite(M_REF, HIGH); }
    if (mode == M_LOGIC) { pcntStart(M_PIN); winT = millis(); hz = 0; logic = 2; }
    else analogSetPinAttenuation(M_PIN, ADC_11db);
    if (mode == M_CAP) capStart();
    rel = false; note = ""; wiredN = 0; floatV = false; floatT = 0; open = mode != M_VOLT && mode != M_BATT;
    resetStats();
    if (!(seen & (1 << mode))) { seen |= 1 << mode; guide = true; stateDirty = true; }
  }
  void resetStats() { vmin = 1e9f; vmax = -1e9f; vsum = 0; vn = 0; tn = 0; }
  void enter() override { setup(); }
  void leave() override { pinsOff(); }

  int readMv(int n = 32) {  // averaged, calibrated millivolts on the probe pin
    uint32_t s = 0;
    for (int i = 0; i < n; i++) s += analogReadMilliVolts(M_PIN);
    return s / n;
  }
  // pull the probe pin up, then down, and read it: a pin with nothing on it follows the pull (~3 V, then ~0 V),
  // anything really connected holds its voltage. (Pulls set directly, so the ADC stays set up.)
  void pullTest(int& up, int& dn) {
    gpio_num_t g = (gpio_num_t)M_PIN;
    bool rtc = rtc_gpio_is_valid_gpio(g);  // analog pads take their pulls from the RTC IO block
    auto pull = [&](bool u, bool d) {
      if (u) gpio_pullup_en(g); else gpio_pullup_dis(g);
      if (d) gpio_pulldown_en(g); else gpio_pulldown_dis(g);
      if (rtc) { if (u) rtc_gpio_pullup_en(g); else rtc_gpio_pullup_dis(g); if (d) rtc_gpio_pulldown_en(g); else rtc_gpio_pulldown_dis(g); }
    };
    pull(true, false); delayMicroseconds(150); up = readMv(4);
    pull(false, true); delayMicroseconds(150); dn = readMv(4);
    pull(false, false); delayMicroseconds(100);
  }
  // a low reading can also mean "nothing wired at all" (a floating pin reads ~0 V): with the drive pin off and
  // the pull-up on the probe, a real part to GND keeps the pin LOW, a loose pin goes HIGH
  bool floating() {
    pinMode(M_REF, INPUT);
    int up, dn; pullTest(up, dn);
    pinMode(M_REF, OUTPUT); digitalWrite(M_REF, HIGH); delayMicroseconds(200);
    return up > 1650;
  }
  // capacitance, without freezing the device: empty the cap through the reference resistor, then switch the
  // drive pin HIGH and read the voltage V after time t. V = VCC (1 - e^(-t/RC))  ->  C = -t / (R ln(1 - V/VCC)).
  // Small caps finish in a short busy loop, big ones are read again on later ticks.
  enum { C_DIS, C_CHG, C_WAIT } cst = C_DIS;
  uint32_t cT = 0, cUs = 0; int cLow = 0;
  void capStart() { pinMode(M_REF, OUTPUT); digitalWrite(M_REF, LOW); cst = C_DIS; cT = millis(); cLow = 0; }
  bool capDone(int mv, uint32_t us) {  // true = got a value (or a final error)
    const float R = REFS[ref];
    digitalWrite(M_REF, LOW);
    if (us < 120 && mv > 2900) { note = ref < 2 ? "NOTHING, OR SMALL: PRESS R FOR 100K" : "NOTHING, OR UNDER 1NF"; open = true; }
    else { float x = std::min(mv / VCC, 0.95f); val = -(us * 1e-6f) / (R * logf(1 - x)); open = false; note = ""; }
    cst = C_WAIT; cT = millis();
    return true;
  }
  bool capTick() {  // true = a measurement just finished
    const uint32_t now = millis();
    if (cst == C_WAIT) { if (now - cT >= 250) capStart(); return false; }
    if (cst == C_DIS) {
      int mv = readMv(4);
      if (mv <= 15) { if (++cLow >= 2) {  // empty (twice in a row): start charging
          uint32_t t0 = micros();
          digitalWrite(M_REF, HIGH);
          for (;;) {  // busy loop for at most 20 ms
            int v = analogReadMilliVolts(M_PIN);
            uint32_t us = micros() - t0;
            if (v >= 1300) return capDone(v, us);
            if (us > 20000) break;
          }
          cst = C_CHG; cUs = t0; cT = now;
        }
        return false;
      }
      cLow = 0;
      if (now - cT > 300) {
        int up, dn; pullTest(up, dn);  // a loose pin follows the pulls, a charged cap doesn't
        if (up - dn > 1800) { digitalWrite(M_REF, LOW); open = true; note = "NOT CONNECTED"; cst = C_WAIT; cT = now; return true; }
        note = "DISCHARGING...";
      }
      if (now - cT > 8000) {
        note = floating() ? "NOT CONNECTED" : "WON'T DISCHARGE";
        digitalWrite(M_REF, LOW); open = true; cst = C_WAIT; cT = now;
        return true;
      }
      return false;
    }
    // C_CHG: slow cap
    uint32_t a = micros(); int mv = readMv(4); uint32_t us = (a + micros()) / 2 - cUs;
    if (mv >= 1300) return capDone(mv, us);
    if (now - cT > 300) note = "MEASURING...";
    if (now - cT > 3000) {
      if (mv >= 150) return capDone(mv, us);
      digitalWrite(M_REF, LOW); open = true; cst = C_WAIT; cT = now;
      note = ref ? "NOT WIRED, OR BIG: PRESS R FOR 1K" : "NOT WIRED, OR OVER 5000UF";
      return true;
    }
    return false;
  }
  // drive HIGH and LOW: a wired circuit follows the drive pin, a loose probe pin doesn't. If nothing changes it is
  // either a short to GND (pull-up test stays LOW) or not connected. Returns false = OPEN / not wired.
  bool wiredRead(int& mv) {
    mv = readMv();
    if (mv > 3000) return false;
    digitalWrite(M_REF, LOW); delayMicroseconds(400);
    int lo = readMv(8);
    digitalWrite(M_REF, HIGH); delayMicroseconds(400);
    if (mv - lo > 150 && lo < 80) return true;
    return !floating();  // stuck low: short (true) or loose (false)
  }
  int wiredN = 0; bool floatV = false; uint32_t floatT = 0;  // a loose pin can pass once by chance: show a value only after 3 good readings in a row
  bool steady(bool w) { wiredN = w ? std::min(wiredN + 1, 3) : 0; return wiredN >= 3; }
  void sample() {
    if (mode == M_CAP) {  // keeps its own state across ticks; stats only when a measurement finishes
      if (!capTick()) return;
      if (open) val = 0;
      goto stats;
    }
    over = open = false;
    note = "";
    if (mode == M_VOLT) {
      if (millis() - floatT > 300) { floatT = millis(); int up, dn; pullTest(up, dn); floatV = up - dn > 1800; }
      int mv = readMv();
      over = mv > 3050;
      val = mv / 1000.f * DIVS[div];
      if (floatV && !over) { open = true; val = 0; note = "NOTHING ON THE PIN (LOOSE WIRE?)"; }
    } else if (mode == M_OHM) {
      int mv;
      if (!steady(wiredRead(mv))) { open = true; val = 0; }
      else {
        val = REFS[ref] * mv / std::max(1.f, VCC - mv);
        if (val > REFS[ref] * 20 && ref < 2) note = "HIGH: USE A BIGGER REF";
        else if (val < REFS[ref] / 20.f && val > 60 && ref > 0) note = "LOW: USE A SMALLER REF";
        if (beep && val < 50 && millis() - beepT > 140) { beepT = millis(); Audio::blip(2200, 60, Audio::W_SQUARE, 0.25f); }
      }
    } else if (mode == M_DIODE) {
      int mv;
      if (!steady(wiredRead(mv))) { open = true; val = 0; note = mv > 3000 ? "OPEN OR REVERSED" : "NOT CONNECTED"; }
      else {
        val = mv / 1000.f;
        note = mv < 100 ? "SHORT" : mv < 450 ? "SCHOTTKY / GERMANIUM" : mv < 950 ? "SILICON DIODE" : mv < 1500 ? "2 DIODES / DARLINGTON" :
               mv < 2100 ? "LED (RED / YELLOW)" : "LED (GREEN / BLUE / WHITE)";
        if (beep && mv < 100 && millis() - beepT > 140) { beepT = millis(); Audio::blip(2200, 60, Audio::W_SQUARE, 0.25f); }
      }
    } else if (mode == M_LOGIC) {
      pcntPoll();
      uint32_t now = millis();
      if (now - winT >= 500) {  // count edges for half a second, then look at the level
        hz = pcTotal * 1000.f / (now - winT);
        gpio_num_t g = (gpio_num_t)M_PIN;
        gpio_pullup_en(g); delayMicroseconds(80); int a = gpio_get_level(g);
        gpio_pullup_dis(g); gpio_pulldown_en(g); delayMicroseconds(80); int b = gpio_get_level(g);
        gpio_pulldown_dis(g);
        logic = (a && b) ? 1 : (!a && !b) ? 0 : 2;
        if (logic == 2) hz = 0;          // a loose wire picks up hum: don't call that a signal
        else if (hz > 2) logic = 3;
        delayMicroseconds(50); pcntPoll(); pcTotal = 0; winT = millis();
      }
      val = hz;
      if (logic == 2) note = "FLOATING: NOTHING DRIVES THE PIN";
    } else {
      val = Batt::mv / 1000.f;
    }
  stats:
    if (rel && !open && !over) val -= relV;
    bool valid = !over && !open;
    if (valid) { vmin = std::min(vmin, val); vmax = std::max(vmax, val); vsum += val; vn++; }
    if (tn < 200) trace[tn++] = valid ? val : NAN;
    else { memmove(trace, trace + 1, sizeof(float) * 199); trace[199] = valid ? val : NAN; }
  }
  void tick() override {
    if (hold) return;
    if (millis() - lastT < 50) return;
    lastT = millis();
    if (mode == M_BATT) Batt::tick();
    sample();
  }
  bool key(const KeyEv& e) override {
    if (help) { help = false; return true; }
    if (guide) { guide = false; return true; }
    if (e.code == K_ENTER) { guide = true; return true; }
    if (pinMap) { pinMap = false; return true; }
    if (e.is('p')) { pinsOff(); probe = (probe + 1) % NMP; if (probe == drive) drive = (drive + 1) % NMP; setup(); pinToast(); return true; }
    if (e.is('d')) { pinsOff(); do drive = (drive + 1) % NMP; while (drive == probe); setup(); pinToast(); return true; }
    if (e.is('m')) { pinMap = true; return true; }
    KeyCode k = e.nav();
    if (e.code == K_TAB || k == K_RIGHT) { mode = (mode + 1) % M_N; setup(); return true; }
    if (k == K_LEFT) { mode = (mode + M_N - 1) % M_N; setup(); return true; }
    if (e.code == K_CHAR && e.ch >= '1' && e.ch <= '0' + M_N) { mode = e.ch - '1'; setup(); return true; }
    if (e.is('r') || k == K_UP || k == K_DOWN) {
      int d = k == K_DOWN ? 2 : 1;
      if (mode == M_VOLT) { div = (div + d) % 3; resetStats(); }
      else if (usesDrive()) { ref = (ref + d) % 3; resetStats(); wiredN = 0; if (mode == M_CAP) { open = true; note = ""; capStart(); }
        char b[24]; snprintf(b, sizeof(b), "REF RESISTOR %dK", REFS[ref] / 1000); toast(b, 900); }
      return true;
    }
    if (e.is('h') || (e.code == K_CHAR && e.ch == ' ')) { hold = !hold; toast(hold ? "HOLD" : "LIVE", 600); return true; }
    if (e.is('z')) { resetStats(); toast("MIN / MAX RESET", 700); return true; }
    if (e.code == K_CHAR && e.ch == '0') {  // REL / zero: e.g. the resistance of the test leads
      if (rel) { rel = false; toast("REL OFF", 700); }
      else if (!open && !over) { relV = val; rel = true; toast("REL: ZEROED", 700); }
      resetStats(); return true;
    }
    if (e.is('b')) { beep = !beep; toast(beep ? "CONTINUITY BEEP ON" : "BEEP OFF", 800); return true; }
    if (e.code == K_CHAR && e.ch == '?') { help = true; return true; }
    return false;
  }
  void pinToast() {
    char b[40];
    if (usesDrive()) snprintf(b, sizeof(b), "PROBE G%d  DRIVE G%d", MPINS[probe].gpio, MPINS[drive].gpio);
    else snprintf(b, sizeof(b), "PROBE G%d (%s)", MPINS[probe].gpio, MPINS[probe].where);
    toast(b, 1200);
  }
  // step-by-step wiring for the current mode (ENTER, and the first time a mode is opened)
  void drawGuide() {
    background();
    static const char* MN[M_N] = {"VOLTS", "RESISTANCE", "DIODE", "CAPACITOR", "LOGIC", "BATTERY"};
    char t[32]; snprintf(t, sizeof(t), "HOW TO: %s", MN[mode]);
    statusBar(t, T.yel);
    const int P = MPINS[probe].gpio, D = MPINS[drive].gpio;
    char l[6][48] = {};
    const char* part = mode == M_OHM ? "THE PART" : mode == M_DIODE ? "THE DIODE" : "THE CAP";
    if (mode == M_VOLT) {
      snprintf(l[0], 48, "1 WIRE GND (EXT PIN 4) TO THE - SIDE");
      snprintf(l[1], 48, "2 WIRE G%d (%s) TO THE + SIDE", P, MPINS[probe].where);
      snprintf(l[2], 48, "3 READ THE VOLTS. MAX 3.1 V!");
      snprintf(l[3], 48, "  MORE VOLTS: A DIVIDER + R (X2 X11)");
      snprintf(l[4], 48, "  E.G. AA BATTERY: + TO G%d, - TO GND", P);
    } else if (usesDrive()) {
      snprintf(l[0], 48, "1 PUT A %dK RESISTOR FROM G%d TO G%d", REFS[ref] / 1000, D, P);
      snprintf(l[1], 48, "2 PUT %s FROM G%d TO GND", part, P);
      if (mode == M_OHM) {
        snprintf(l[2], 48, "3 READ THE OHMS. 0 OHM = BEEP");
        snprintf(l[3], 48, "  R = REF 1K/10K/100K: SET IT TO THE");
        snprintf(l[4], 48, "  RESISTOR YOU REALLY USED IN STEP 1");
      } else if (mode == M_DIODE) {
        snprintf(l[1], 48, "2 DIODE FROM G%d, STRIPE SIDE TO GND", P);
        snprintf(l[2], 48, "3 READ THE VOLTS + THE DIODE TYPE");
        snprintf(l[3], 48, "  LED: LONG LEG TO G%d, SHORT TO GND", P);
        snprintf(l[4], 48, "  R = SET THE REF YOU USED (10K OK)");
      } else {
        snprintf(l[1], 48, "2 CAP FROM G%d, ITS - LEG TO GND", P);
        snprintf(l[2], 48, "3 WAIT: BIG CAPS TAKE A FEW SECONDS");
        snprintf(l[3], 48, "  R = REF: 100K FOR NF, 1K FOR BIG UF");
        snprintf(l[4], 48, "  SET THE REF YOU REALLY USED");
      }
    } else if (mode == M_LOGIC) {
      snprintf(l[0], 48, "1 WIRE GND (EXT PIN 4) TO THE CIRCUIT GND");
      snprintf(l[1], 48, "2 TOUCH G%d TO THE SIGNAL (3.3V MAX)", P);
      snprintf(l[2], 48, "3 SHOWS HIGH / LOW / FLOAT, OR THE");
      snprintf(l[3], 48, "  FREQUENCY IF IT TOGGLES (UP TO 300KHZ)");
    } else {
      snprintf(l[0], 48, "NOTHING TO WIRE: SHOWS THIS");
      snprintf(l[1], 48, "CARDPUTER'S OWN BATTERY VOLTAGE");
    }
    for (int i = 0; i < 5; i++) if (l[i][0]) textFit(6, 18 + i * 12, l[i], l[i][0] == ' ' ? T.grey : T.white, 230, false);
    if (mode != M_BATT) {
      panel(4, 80, 232, 26, T.edge, T.bg0);
      char w[48];
      if (mode == M_VOLT || mode == M_LOGIC) snprintf(w, 48, "G%d -> +    GND (PIN 4) -> -", P);
      else snprintf(w, 48, "G%d -[%dK]- G%d -[%s]- GND", D, REFS[ref] / 1000, P, mode == M_OHM ? "PART" : mode == M_DIODE ? ">|" : "||");
      textC(120, 84, w, T.yel, 1, false);
      textC(120, 95, "M = PIN MAP    P = CHANGE PIN", T.dim, 1, false);
    }
    textC(120, 110, "NEVER WIRE 5V TO A G PIN!", T.red, 1, false);
    hintBar({{"ANY KEY", "START"}});
  }
  void drawPinMap() {  // the 14-pin EXT header as seen from the back, safe probe pins highlighted
    background();
    statusBar("EXT HEADER PINS", T.yel);
    static const char* L[7] = {"G3", "G4", "G6", "G40", "G14", "G39", "G5"};
    static const char* R[7] = {"5VIN", "GND", "5VOUT", "G8", "G9", "G13", "G15"};
    static const char* LN[7] = {"", "", "", "SD", "SD", "SD", ""};
    static const char* RN[7] = {"5V!", "", "5V!", "I2C", "I2C", "", ""};
    for (int r = 0; r < 7; r++) {
      int y = 16 + r * 13;
      for (int sIdx = 0; sIdx < 2; sIdx++) {
        const char* n = sIdx ? R[r] : L[r];
        int pinNo = r * 2 + 1 + sIdx;
        bool use = false, sel = false;
        for (int i = 0; i < NMP; i++) if (n[0] == 'G' && MPINS[i].gpio == atoi(n + 1)) { use = true; sel = i == probe || (usesDrive() && i == drive); }
        bool gnd = !strcmp(n, "GND"), pwr = n[0] == '5';
        uint16_t c = sel ? T.yel : use ? T.green : gnd ? T.white : pwr ? T.red : T.dim;
        int x = sIdx ? 124 : 18;
        char b[12]; snprintf(b, sizeof(b), "%2d", pinNo);
        if (!sIdx) { text(x, y, b, T.dim, 1, false); textFit(x + 16, y, n, c, 40, false); text(x + 52, y, LN[r], T.dim, 1, false); cv.fillCircle(104, y + 3, 3, c); }
        else { cv.fillCircle(116, y + 3, 3, c); textFit(x, y, n, c, 40, false); text(x + 38, y, b, T.dim, 1, false); text(x + 56, y, RN[r], pwr ? T.red : T.dim, 1, false); }
      }
    }
    textC(120, 110, "GREEN = PROBE PINS  YELLOW = IN USE", T.dim, 1, false);
    hintBar({{"ANY KEY", "BACK"}});
  }
  // number + unit with an SI prefix (K M / M U N P)
  static void fmtSI(char* b, int n, float v, int digits = 3) {
    float a = fabsf(v);
    const char* p = "";
    if (a >= 1e6f) { v /= 1e6f; p = "M"; } else if (a >= 1e3f) { v /= 1e3f; p = "K"; }
    else if (a > 0 && a < 1e-9f) { v *= 1e12f; p = "P"; } else if (a > 0 && a < 1e-6f) { v *= 1e9f; p = "N"; }
    else if (a > 0 && a < 1e-3f) { v *= 1e6f; p = "U"; } else if (a > 0 && a < 1) { v *= 1e3f; p = "M"; }
    a = fabsf(v);
    int dec = a >= 100 ? 0 : a >= 10 ? 1 : 2;
    if (digits > 3 && dec < 3) dec++;
    snprintf(b, n, "%.*f%s", dec, v, p);
  }
  void fmtVal(char* b, int n, float v) {
    if (mode == M_VOLT || mode == M_DIODE || mode == M_BATT) snprintf(b, n, fabsf(v) < 10 ? "%.3f" : "%.2f", v);
    else if (mode == M_LOGIC) { if (v >= 1000) snprintf(b, n, "%.2fK", v / 1000); else snprintf(b, n, "%.0f", v); }
    else fmtSI(b, n, v);
  }
  float fullScale() const {
    switch (mode) {
      case M_VOLT: return 3.1f * DIVS[div];
      case M_DIODE: return 3.0f;
      case M_BATT: return 4.2f;  // (drawn from 3.3 V up)
      default: return 0;  // log scale
    }
  }
  void drawBar(int y) {  // analog-style bar like a real meter
    float f;
    if (open || over) f = over ? 1 : 0;
    else if (mode == M_OHM) f = val <= 1 ? 0 : log10f(val) / 7;  // 1 ohm .. 10 M
    else if (mode == M_CAP) f = val <= 1e-10f ? 0 : (log10f(val) + 10) / 8;  // 100 pF .. 10 mF
    else if (mode == M_LOGIC) f = val <= 1 ? 0 : log10f(val) / 6;
    else if (mode == M_BATT) f = (val - 3.3f) / 0.9f;
    else f = fabsf(val) / fullScale();
    f = constrain(f, 0.f, 1.f);
    int w = 180, x0 = 30, n = (int)(f * w);
    bool warn = mode == M_VOLT || mode == M_DIODE;  // red end = near the top of the range
    for (int i = 0; i < w; i += 3) cv.fillRect(x0 + i, y, 2, 4, i < n ? (!warn ? T.cyan : i > w * 0.9f ? T.red : i > w * 0.75f ? T.yel : T.green) : T.dim);
  }
  void draw() override {
    if (pinMap) { drawPinMap(); return; }
    if (guide) { drawGuide(); return; }
    background();
    statusBar("METER", T.yel);
    static const char* MN[M_N] = {"VOLT", "OHM", "DIODE", "CAP", "LOGIC", "BATT"};
    for (int i = 0; i < M_N; i++) {  // mode tabs
      int x = 3 + i * 39, w = 37;
      bool s = i == mode;
      if (s) panel(x, 14, w, 12, T.yel, lerpc(T.panel, T.yel, 0.15f)); else panel(x, 14, w, 12, T.edge);
      textC(x + w / 2, 16, MN[i], s ? T.white : T.dim, 1, false);
    }
    char b[24], u[24] = "";
    uint16_t c = T.white;
    static const char* UN[M_N] = {"V", "OHM", "V", "F", "HZ", "V"};
    const char* unit = UN[mode];
    if (mode == M_VOLT && over) { snprintf(b, sizeof(b), ">%.1f", 3.05f * DIVS[div]); c = T.red; }
    else if (open && mode == M_CAP) { snprintf(b, sizeof(b), "----"); unit = ""; c = T.dim; }
    else if (open) { snprintf(b, sizeof(b), "OPEN"); unit = ""; c = T.dim; }
    else if (mode == M_LOGIC && logic != 3) {
      snprintf(b, sizeof(b), "%s", logic == 1 ? "HIGH" : logic == 0 ? "LOW" : "FLOAT");
      c = logic == 1 ? T.green : logic == 0 ? T.cyan : T.yel; unit = "";
    } else fmtVal(b, sizeof(b), val);
    if (!open && ((mode == M_OHM && val < 50) || (mode == M_DIODE && val < 0.1f))) c = T.green;  // continuity
    int w = textW(b, 3) + (unit[0] ? 4 + textW(unit, 2) : 0);
    int ex = text(120 - w / 2, 31, b, c, 3);
    if (unit[0]) text(ex + 4, 39, unit, T.grey, 2);
    // badges on the right
    int by = 31;
    if (hold) { text(210, by, "HOLD", (millis() / 400) % 2 ? T.yel : T.dim, 1, false); by += 10; }
    if (rel) { text(216, by, "REL", T.cyan, 1, false); by += 10; }
    // left: range / details
    if (mode == M_VOLT) snprintf(u, sizeof(u), "X%d", (int)DIVS[div]);
    else if (usesDrive()) snprintf(u, sizeof(u), "REF %dK", REFS[ref] / 1000);
    else if (mode == M_LOGIC) snprintf(u, sizeof(u), logic == 3 ? "TOGGLE" : "%.0f HZ", hz);
    else snprintf(u, sizeof(u), "%d%% %s", Batt::pct, Batt::charging ? "CHG" : Batt::usb ? "USB" : "");
    text(4, 31, u, T.dim, 1, false);
    drawBar(57);
    if (note[0]) textC(120, 63, note, mode == M_DIODE && !open ? T.cyan : T.yel, 1, false);
    else if (vn && mode != M_LOGIC) {
      char a1[12], a2[12], a3[12], l[48];
      fmtVal(a1, sizeof(a1), vmin); fmtVal(a2, sizeof(a2), vsum / vn); fmtVal(a3, sizeof(a3), vmax);
      snprintf(l, sizeof(l), "MIN %s  AVG %s  MAX %s", a1, a2, a3);
      textC(120, 63, l, T.grey, 1, false);
    }
    // trace of the last 10 seconds
    panel(4, 73, 232, 36, T.edge, T.bg0);
    float lo = 1e30f, hi = -1e30f;
    for (int i = 0; i < tn; i++) if (!isnan(trace[i])) { lo = std::min(lo, trace[i]); hi = std::max(hi, trace[i]); }
    if (hi < lo) { lo = 0; hi = 1; }
    if (hi - lo < fabsf(hi) * 1e-3f + 1e-12f) { float m = fabsf(hi) * 0.05f + 1e-12f; hi += m; lo -= m; }
    int py = -1;
    for (int i = 0; i < tn; i++) {
      int x = 6 + i * 228 / 200;
      if (isnan(trace[i])) { py = -1; continue; }
      int y = 106 - (int)((trace[i] - lo) / (hi - lo) * 30);
      if (py >= 0) cv.drawLine(x - 1, py, x, y, T.yel); else cv.drawPixel(x, y, T.yel);
      py = y;
    }
    // wiring help for this mode
    char wire[48];
    int P = MPINS[probe].gpio, D = MPINS[drive].gpio;
    if (mode == M_VOLT) {
      if (div == 0) snprintf(wire, sizeof(wire), "G%d (%s) = +  GND = -  MAX 3.1V", P, MPINS[probe].where);
      else if (div == 1) snprintf(wire, sizeof(wire), "+ -R- G%d -R- GND  (2 SAME R)", P);
      else snprintf(wire, sizeof(wire), "+ -100K- G%d -10K- GND  MAX 34V", P);
    } else if (mode == M_OHM) snprintf(wire, sizeof(wire), "G%d -[REF]- G%d -[PART]- GND", D, P);
    else if (mode == M_DIODE) snprintf(wire, sizeof(wire), "G%d -[REF]- G%d -|>|- GND", D, P);
    else if (mode == M_CAP) snprintf(wire, sizeof(wire), "G%d -[REF]- G%d -||- GND  (- TO GND)", D, P);
    else if (mode == M_LOGIC) snprintf(wire, sizeof(wire), "PROBE G%d (%s)  3.3V LOGIC", P, MPINS[probe].where);
    else snprintf(wire, sizeof(wire), "CARDPUTER BATTERY");
    textC(120, 112, wire, T.dim, 1, false);
    hintBar({{"<>", "MODE"}, {"ENT", "HOW TO"}, {"R", usesDrive() ? "REF" : "RANGE"}, {"0", "REL"}, {"?", "KEYS"}});
    if (help)
      helpOverlay("METER", {"<> TAB 1-6  MODE", "ENTER  HOW TO WIRE IT", "P  PROBE PIN", "D  DRIVE PIN", "M  EXT PIN MAP", "R ^v  RANGE / REF", "0  REL (ZERO)",
                            "H SPC  HOLD", "Z  RESET MIN/MAX", "B  CONTINUITY BEEP", "PINS  G1 G2 GROVE", "EXT  G3 G4 G5 G6 G13 G15",
                            "GND  EXT PIN 4", "REF  1K 10K 100K", "X2  TWO SAME RESISTORS", "X11  100K + 10K", "CAP  1.5NF - 1500UF",
                            "MAX 3.3V ON ANY PIN", "5V PINS: NEVER ON A PROBE"});
  }
};
constexpr float MeterApp::DIVS[3];
constexpr int MeterApp::REFS[3];

App* appMeter = new MeterApp();
