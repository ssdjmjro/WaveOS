// WAVE OS - keyboard events with key repeat (works on Cardputer + Cardputer ADV)
#include "common.h"

std::vector<char> heldKeys;
bool heldFn = false, heldShift = false;

KeyCode KeyEv::arrow() const {
  switch (ch) {
    case ';': return K_UP;
    case '.': return K_DOWN;
    case ',': return K_LEFT;
    case '/': return K_RIGHT;
  }
  return K_NONE;
}

struct HK { int x, y; uint32_t t0, tl; };
static std::vector<HK> prevKeys;

static bool isMod(uint8_t v) {
  return v == KEY_FN || v == KEY_LEFT_SHIFT || v == KEY_LEFT_CTRL || v == KEY_LEFT_ALT || v == KEY_OPT;
}

std::vector<KeyEv> pollKeys() {
  std::vector<KeyEv> out;
  auto& kb = M5Cardputer.Keyboard;
  const auto& list = kb.keyList();
  bool fn = false, sh = false, ctrl = false, alt = false, opt = false;
  for (auto& p : list) {
    uint8_t v = (uint8_t)kb.getKeyValue(p).value_first;
    if (v == KEY_FN) fn = true;
    else if (v == KEY_LEFT_SHIFT) sh = true;
    else if (v == KEY_LEFT_CTRL) ctrl = true;
    else if (v == KEY_LEFT_ALT) alt = true;
    else if (v == KEY_OPT) opt = true;
  }
  heldFn = fn;
  heldShift = sh;
  heldKeys.clear();
  uint32_t now = millis();
  std::vector<HK> next;
  for (auto& p : list) {
    KeyValue_t kv = kb.getKeyValue(p);
    uint8_t v = (uint8_t)kv.value_first;
    if (isMod(v)) continue;
    heldKeys.push_back((char)v);
    bool fire = false, rep = false;
    HK hk{p.x, p.y, now, now};
    bool found = false;
    for (auto& o : prevKeys)
      if (o.x == p.x && o.y == p.y) { hk = o; found = true; break; }
    if (!found) fire = true;
    else if (now - hk.t0 > 420 && now - hk.tl > 65) { fire = true; rep = true; hk.tl = now; }
    next.push_back(hk);
    if (!fire) continue;
    KeyEv e;
    e.fn = fn; e.shift = sh; e.ctrl = ctrl; e.alt = alt; e.opt = opt; e.rep = rep;
    if (v == KEY_BACKSPACE) e.code = K_BACK;
    else if (v == KEY_TAB) e.code = K_TAB;
    else if (v == KEY_ENTER) e.code = K_ENTER;
    else if (v == '`' && !sh) { e.code = K_ESC; e.ch = '`'; }
    else {
      e.code = K_CHAR;
      e.ch = (sh || kb.capslocked()) ? kv.value_second : kv.value_first;
    }
    out.push_back(e);
  }
  prevKeys = next;
  return out;
}
