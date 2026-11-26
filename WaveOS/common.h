// WAVE OS - shared declarations
#pragma once
#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>
#include <vector>
#include <initializer_list>

#define FW_NAME "WAVE OS"
#define FW_VER  "1.24"

// ---------------------------------------------------------------- theme
struct Theme {
  uint16_t bg0, bg1, bg2, panel, edge, cyan, cyanD, blue, blueD, purp, purpD,
           pink, white, grey, dim, green, yel, red, bar;
};
extern Theme T;
void applyTheme(int theme);
bool customThemeExists();

constexpr uint16_t RGB(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
uint16_t lerpc(uint16_t a, uint16_t b, float t);
uint16_t gradc(float t);  // cyan -> blue -> purple

extern M5Canvas cv;  // full screen frame buffer
extern bool sdOK;

// ---------------------------------------------------------------- gfx
int  textW(const char* s, int sc = 1);
int  text(int x, int y, const char* s, uint16_t c, int sc = 1, bool sh = true);
void textC(int cx, int y, const char* s, uint16_t c, int sc = 1, bool sh = true);
void textFit(int x, int y, const char* s, uint16_t c, int maxw, bool sh = true);
void frame(int x, int y, int w, int h, uint16_t c);
void panel(int x, int y, int w, int h, uint16_t edge, uint16_t fill);
void panel(int x, int y, int w, int h, uint16_t edge);
void glowFrame(int x, int y, int w, int h, uint16_t c);
void background(bool plain = false);
void rebuildBackground();
void drawWave(int x0, int x1, int cy, float amp, float phase, bool glow, float reveal = 1.0f);
void statusBar(const char* title, uint16_t accent);
struct Hint { const char* k; const char* l; };
void hintBar(std::initializer_list<Hint> h);
void drawIcon(int x, int y, int idx, int sc, uint16_t c);
void drawOhm(int x, int y, uint16_t c, int sc = 1);
void toast(const char* msg, uint32_t ms = 1400);
void drawToast();
void helpOverlay(const char* title, std::initializer_list<const char*> lines);

enum IconId { IC_MUSIC, IC_SYNTH, IC_MIDI, IC_IR, IC_TOOLS, IC_NOTES, IC_GAMES, IC_SETUP,
              IC_CALC, IC_GRAPH, IC_RES, IC_SNAKE, IC_2048, IC_GUITAR, IC_ELEC, IC_BLOCKS,
              IC_BREAKOUT, IC_FLAPPY, IC_TYPING, IC_RHYTHM, IC_DRUMS, IC_STARFALL, IC_MINES,
              IC_DUEL, IC_ECHO, IC_RUNNER, IC_MAZE, IC_FOUR, IC_TANKS, IC_ROCKS, IC_HOPPER, IC_TIMER, IC_FUN, IC_SUDOKU, IC_WORDS, IC_PET, IC_DOODLE,
              IC_TTT, IC_BUBBLES, IC_ORACLE, IC_WIFI, IC_FLASH, IC_MORSE,
              IC_LIGHTS, IC_SLIDE, IC_REFLEX, IC_CODE, IC_FIVE, IC_LIFE, IC_METRO, IC_LEVEL, IC_CONV, IC_DECIDE, IC_COUNT };

// ---------------------------------------------------------------- input
enum KeyCode : uint8_t { K_NONE, K_CHAR, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_ENTER, K_BACK, K_ESC, K_TAB };
struct KeyEv {
  KeyCode code = K_NONE;
  char ch = 0;
  bool fn = false, shift = false, ctrl = false, alt = false, opt = false, rep = false;
  KeyCode arrow() const;                       // ; . , /  ->  up down left right
  KeyCode nav() const { return code == K_CHAR ? arrow() : code; }            // menus
  KeyCode navFn() const { return code == K_CHAR ? (fn ? arrow() : K_NONE) : code; }  // text mode
  bool is(char c) const { return code == K_CHAR && !fn && (ch == c || ch == (c ^ 0x20)); }
};
std::vector<KeyEv> pollKeys();
extern std::vector<char> heldKeys;  // un-shifted values of held non-modifier keys
extern bool heldFn, heldShift;

// ---------------------------------------------------------------- apps
struct App {
  const char* name = "";
  App* parent = nullptr;
  virtual void enter() {}
  virtual void leave() {}
  virtual bool key(const KeyEv&) { return false; }  // true = handled
  virtual void tick() {}
  virtual void draw() = 0;
  virtual bool wantsAllKeys() { return false; }     // true = no key click
  virtual bool ownsPlusMinus() { return wantsAllKeys(); }  // false = - and = change the volume
  virtual ~App() {}
};
void openApp(App* a);
void goBack();
void goHome();
extern App *appHome, *appMusic, *appSynth, *appIR, *appTools, *appNotes, *appGames,
           *appSettings, *appCalc, *appGraph, *appResistor, *appSnake, *app2048,
           *appMetro, *appElec, *appBlocks, *appBreakout, *appFlappy, *appTyping, *appRhythm,
           *appDrums, *appStarfall, *appMines, *appDuel, *appEcho, *appRunner,
           *appMaze, *appFour, *appTanks, *appRocks, *appHopper, *appTimer,
           *appFun, *appSudoku, *appWords, *appPet, *appDoodle, *appTtt, *appBubbles, *appOracle,
           *appWifi, *appFlash, *appMorse,
           *appLights, *appSlide, *appReflex, *appCode, *appFive, *appLife,
           *appLevel, *appConvert, *appDecide;

// ---------------------------------------------------------------- settings
struct Settings {
  uint8_t volume = 140;
  uint8_t maxVol = 255;   // volume limit (headphone safety)
  uint8_t bright = 160;
  bool mono = false;      // theme 1
  uint8_t theme = 0;      // 0 neon, 1 mono, 2 custom (/themes/custom.txt)
  bool click = true;
  uint8_t irRx = 1;       // Grove pin for IR receiver (G1 or G2)
  bool shake = true;      // shake to shuffle
  uint8_t dimSec = 60;    // screensaver after N seconds (0 = never)
  uint8_t sleepMin = 10;  // deep sleep after N minutes idle, no music (0 = never)
  uint8_t midiCh = 1;
  uint8_t rot = 3;        // display rotation (1 or 3 = flipped)
  uint8_t musicOff = 20;  // screen off after N s idle while music plays (0 = never)
  bool mute = false;      // class mode: Fn+M
  bool bootSound = false; // startup chime
};
extern Settings cfg;
void saveSettings();
void loadSettings();

// ---------------------------------------------------------------- util
String fmtTime(uint32_t sec);
void ensureDir(const char* p);
String baseName(const String& path);
String parentName(const String& path);
void sendToGraph(const char* expr);  // calculator -> graph
void invalidateFrame();               // next frame is pushed in full (after drawing straight to the display)
void volumeChange(int dir);          // +1 / -1, shows a volume overlay
void applyVolume();                  // volume or 0 when muted (Fn+M)
String clockString();
void memLog(const char* where);       // append free-heap numbers to /waveos/mem.txt                // "HH:MM" once the time was set over WiFi, else ""
namespace Batt { extern float mv; extern int pct; extern bool usb, charging; void tick(); }
void sdWriteAsync(const char* path, String* data);  // takes ownership; written in the background
bool sdWriteBusy();
void saveSettingsLater();
int loadScore(const char* key);
void saveScore(const char* key, int val, bool isBest = true);  // isBest: NEW BEST toast + fanfare
bool imuAccel(float& ax, float& ay, float& az);
void searchSongs(const String& q, std::vector<int>& out, int max);
String songPath(int idx);
void playSong(int idx);
void libEnsureScanned();
int notesList(std::vector<String>& out);
void openNote(const String& name);
