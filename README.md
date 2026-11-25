# 〰️ WAVE OS

Custom firmware for the **M5Stack Cardputer ADV** (ESP32-S3), with a polished pixel-art look and a neon wave theme.

## Features
- **Music player:** MP3, FLAC, AAC and WAV, fast and memory-light for large libraries (400+ songs). Songs, folders (by genre/artist), favourites, M3U playlists, queue, most played and recently played. Album art from tags or cover.jpg, synced `.lrc` lyrics, shuffle, repeat, 8 EQ presets, A-B repeat, speed 0.5–2×, sleep timer, bookmarks and resume. Power-saving screen-off and 80 MHz playback mode.
- **Synth:** 5 waveforms, ADSR, resonant filter, delay, arpeggiator, octave layering and hold. Keys are labelled relative to the key you pick (1 2b 2 3b 3 4 5b 5 6b 6 7b 7), and there are 20 chord types (maj, min, 7, maj7, min7, sus2, sus4, 7sus4, aug, dim, dim7, m7b5, mMaj7, 6, m6, add9, 9, maj9, m9, power).
- **4-track looper:** count-in, overdub, quantize and metronome. Saves to SD and exports `.mid`.
- **Drum machine:** 16 steps, 8 synthesized drums, 4 patterns, preset beats and swing. Syncs with the looper.
- **IR remote:** built-in TV codes, learn mode via a Grove IR receiver (any remote, raw capture), a live RECEIVE screen that saves to `.ir`, macros, and a Flipper-format `.ir` file browser. The receiver pin (G1/G2) is found automatically.
- **Tools:** scientific calculator, graphing calculator, resistor color codes, electronics helper (Ohm, LED, divider, 555, capacitor codes), stopwatch/countdown, metronome with tap tempo.
- **Notes:** Markdown editor on SD with autosave.
- **Flashlight** (white/warm/red/strobe/SOS) and **Morse** (type text, hear and see it).
- **17 games:** Starfall, Runner, Snake, Blocks, Merge, Mines, Bricks, Duel, Glider, Echo, Typing, Rhythm, Rocks, Tanks, Four, Hopper, Maze.
- **FUN (quiet, class-friendly):** Sudoku, Words, Pixel Pet, Doodle (saves BMP), Tic Tac Toe (vs CPU / 2 players), Bubbles, Oracle (dice, coin, ask, random picker), Five (word in 6), Code (crack the colours), Lights, Slide (15 puzzle), Reflex and Life. **Fn+M** mutes everything, **Fn+Q** is a boss key that jumps to the calculator.
- **System:** global search, Neon/Mono/Custom themes, smoothed Li-ion battery gauge, screensaver and auto sleep, screenshots (`Fn+P`), crash log.

## Build & flash
Requires `arduino-cli` with the `esp32:esp32` core (3.3.x) and the libraries **M5Cardputer**, **M5Unified**, **M5GFX**, **IRremote** and **ESP8266Audio**.

```bash
arduino-cli compile -b "esp32:esp32:m5stack_cardputer:FlashSize=8M,PartitionScheme=default_8MB,USBMode=hwcdc,CDCOnBoot=cdc" --output-dir build WaveOS
./flash.sh            # backs up the current firmware the first time, then flashes
./flash.sh --restore  # put the backed-up firmware back
```
If flashing fails, hold **G0** while plugging in USB.

## SD card layout
```
/music/<Genre>/<Artist>/*.mp3   /playlists/*.m3u   /midi/*.mid   /ir/**/*.ir
/notes/*.md   /loops   /drums   /themes/custom.txt   /screenshots   /waveos (settings, stats)
```
`tools/organize_music.py` tidies an MP3 collection into this layout, and `tools/add_music.py` adds new songs to an existing card without touching what is already there. `tools/copy_to_sd.sh` syncs it to the card. WiFi file transfer is kept in `extras/`; it is not built in, so more RAM is free for music.

## Keys
`; , . /` are arrows (also with **Fn**). `` ` `` is back. **G0** goes home. **Fn + - / =** changes volume and **Fn + [ / ]** changes brightness. Press `H`, `\` or `?` inside apps for help.
