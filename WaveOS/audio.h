// WAVE OS - one audio engine for everything (music + synth + looper + sfx)
#pragma once
#include <Arduino.h>
#include <vector>

namespace Audio {
constexpr int SR = 32000;    // engine sample rate
constexpr int BLOCK = 512;   // samples per output buffer (16 ms)
constexpr int CHUNK = 64;    // sub-block for sample-accurate-ish timing (2 ms)

void begin();
void setVolume(uint8_t v);
void useDelay();  // allocate the synth echo buffer (lazy, saves RAM)
void suspend(bool s);            // release the speaker (so the mic can use I2S)
void metronome(int bpm, int beats);  // bpm 0 = off
int metroBeatNow();
uint32_t metroFlashT();
constexpr uint8_t OWNER_MIDI = 5;

// -------- synth
enum Wave : uint8_t { W_SINE, W_SQUARE, W_SAW, W_TRI, W_NOISE, W_COUNT };
extern const char* waveNames[W_COUNT];
struct SynthParams {
  float a = 0.01f, d = 0.15f, s = 0.7f, r = 0.25f;  // seconds / level
  float cutoff = 0.75f, reso = 0.2f;                 // 0..1
  float delayMix = 0.2f, delayFb = 0.35f;            // 0..1
  float vol = 0.7f;
};
extern SynthParams sp;
void noteOn(uint8_t note, uint8_t wave, uint8_t owner = 0);
void noteOff(uint8_t note, uint8_t owner = 0);
void allOff(int owner = -1);  // -1 = everything
void blip(float freq, int ms, uint8_t wave = W_SQUARE, float vol = 0.5f);
bool voiceActive(uint8_t note);  // any owner, for the keyboard display
// arpeggiator: mode 0 off, 1 up, 2 down, 3 up-down, 4 random; octave range 1..3
extern volatile int arpMode, arpOct;
void arpNote(uint8_t note, bool on, uint8_t wave);
void arpClear();
// drums: 0 kick 1 snare 2 closed hat 3 open hat 4 clap 5 tom 6 rim 7 cowbell
void drumHit(int type, float vel = 1.0f);
extern float drumVol;

// -------- visualiser taps (last N samples, int16)
constexpr int VIS = 512;
void getScope(int16_t* out, int n, bool music);

}  // namespace Audio

// ------------------------------------------------------------------ looper
namespace Looper {
constexpr int TRACKS = 4;
constexpr int PPQN = 96;
struct Ev { uint32_t t; uint8_t note; uint8_t on; uint16_t session; uint16_t wrap; };
struct Track { std::vector<Ev> ev; uint8_t wave = 2; bool mute = false; uint16_t lastSession = 0; };
extern Track tr[TRACKS];
extern volatile bool playing, recording, counting;
extern volatile int recTrack;
extern int bpm, bars, quant;  // quant: 0 off, 1 = 1/8, 2 = 1/16
extern bool metro;
uint32_t lenTicks();
float posTicks();
float countBeats();  // remaining count-in beats
void play();
void stop();
void toggleRec(int track);
void recNote(uint8_t note, bool on);
void undo(int track);
void clear(int track);
bool empty();
bool save(const char* path);
bool load(const char* path);
bool exportMidi(const char* path, int repeats = 4);
void lock();
void unlock();
}  // namespace Looper

// ------------------------------------------------------------------ music (WAV streaming)
namespace Music {
bool open(const char* path, float startSec = 0);
void close();
void pause(bool p);
bool paused();
bool active();
float pos();
float len();
void seek(float sec);
void setSpeed(float s);
float speed();
void setEQ(float bassDb, float trebleDb);
void setGain(float g);  // fades / sleep timer
bool takeEnded();       // true once when the track reached the end
int srcRate();
int channels();
int bits();
// A-B repeat: state 0 none, 1 A set, 2 A+B looping
void abPress();
int abState();
float abA();
float abB();
void abTick();
const char* format();
const char* error();      // why the last open() failed
uint32_t underrunCount();  // decoder could not keep up (for adaptive CPU speed)
bool seeking();
}  // namespace Music

// ------------------------------------------------------------------ drum machine (16 steps x 8 tracks x 4 patterns)
namespace Drums {
extern uint16_t pat[8][4];
extern volatile int cur, queued;
extern bool mute[8];
extern volatile bool playing;
extern volatile int step;
extern int swing;
void play();
void stop();
}

// ------------------------------------------------------------------ MIDI file player (.mid through the synth)
namespace MidiPlay {
extern volatile bool playing;
bool load(const char* path);
void play();
void stop();
float posSec();
float lenSec();
uint32_t events();
}
