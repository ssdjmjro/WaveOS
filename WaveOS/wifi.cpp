// WAVE OS - WIFI: connect, internet time (clock in the status bar), and a browser file manager
// for the SD card (upload music / notes / .ir files without removing the card).
// WiFi is only switched on while this app is open (saves RAM + battery).
#include "common.h"
#include "audio.h"
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <algorithm>

namespace Net {
int tz = 0;  // hours offset from UTC
bool timeSet() { return time(nullptr) > 1700000000; }
String clock() {
  if (!timeSet()) return String();
  time_t t = time(nullptr) + tz * 3600;
  struct tm tm;
  gmtime_r(&t, &tm);
  char b[8];
  snprintf(b, sizeof(b), "%02d:%02d", tm.tm_hour, tm.tm_min);
  return String(b);
}
}  // namespace Net
String clockString() { return Net::clock(); }

static WebServer* server = nullptr;
static File upFile;
static String upDir = "/music";
static int uploads = 0;

static String htmlEsc(const String& s) { String r = s; r.replace("&", "&amp;"); r.replace("<", "&lt;"); r.replace("\"", "&quot;"); return r; }
static String urlEnc(const String& s) {
  String r; const char* h = "0123456789ABCDEF";
  for (unsigned char c : s) { if (isalnum(c) || strchr("-_./~", c)) r += (char)c; else { r += '%'; r += h[c >> 4]; r += h[c & 15]; } }
  return r;
}
static void handleList() {
  String dir = server->hasArg("d") ? server->arg("d") : "/";
  if (!dir.startsWith("/")) dir = "/" + dir;
  File d = SD.open(dir);
  String p;
  p.reserve(6000);
  p += F("<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'><title>WAVE OS</title><style>"
         "body{background:#04070e;color:#e8f0ff;font-family:monospace;margin:16px}a{color:#3ee8f0;text-decoration:none}"
         "h1{color:#9a5cff;font-size:20px}.f{padding:4px 0;border-bottom:1px solid #1e3050}.d{color:#ffd25a}"
         "button,input{background:#0c1424;color:#e8f0ff;border:1px solid #3ee8f0;padding:6px;margin:4px 0}.x{color:#ff5064}</style></head><body>");
  p += "<h1>~ WAVE OS files</h1><p>" + htmlEsc(dir) + "</p>";
  p += "<form method=POST action='/up?d=" + urlEnc(dir) + "' enctype='multipart/form-data'><input type=file name=f multiple> <button>Upload here</button></form>";
  if (dir != "/") { String up = dir.substring(0, dir.lastIndexOf('/')); if (!up.length()) up = "/"; p += "<div class=f><a href='/?d=" + urlEnc(up) + "'>.. (up)</a></div>"; }
  if (d && d.isDirectory()) {
    std::vector<std::pair<String, bool>> items;
    while (items.size() < 400) {
      File f = d.openNextFile();
      if (!f) break;
      String n = f.name();
      bool isDir = f.isDirectory();
      if (!n.startsWith(".") && n != "System Volume Information") items.push_back({n, isDir});
      f.close();
    }
    std::sort(items.begin(), items.end(), [](const std::pair<String, bool>& a, const std::pair<String, bool>& b) { return a.second != b.second ? a.second : a.first < b.first; });
    for (auto& it : items) {
      String full = (dir == "/" ? "" : dir) + "/" + it.first;
      if (it.second) p += "<div class=f><a class=d href='/?d=" + urlEnc(full) + "'>[" + htmlEsc(it.first) + "]</a></div>";
      else p += "<div class=f><a href='/dl?f=" + urlEnc(full) + "'>" + htmlEsc(it.first) + "</a> <a class=x href='/rm?f=" + urlEnc(full) + "' onclick=\"return confirm('Delete?')\">del</a></div>";
    }
    d.close();
  }
  p += F("<p><form action='/mk'><input name=n placeholder='new folder'><input type=hidden name=d value='");
  p += htmlEsc(dir) + "'><button>Create</button></form></p></body></html>";
  server->send(200, "text/html", p);
}
static void handleUpload() {
  HTTPUpload& u = server->upload();
  if (u.status == UPLOAD_FILE_START) {
    String dir = server->hasArg("d") ? server->arg("d") : "/music";
    String name = u.filename;
    name.replace("/", "_"); name.replace("\\", "_");
    if (!SD.exists(dir)) SD.mkdir(dir);
    upFile = SD.open((dir == "/" ? "" : dir) + "/" + name, FILE_WRITE);
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (upFile) upFile.write(u.buf, u.currentSize);
  } else if (u.status == UPLOAD_FILE_END) {
    if (upFile) { upFile.close(); uploads++; }
  }
}

struct WifiApp : App {
  enum { LIST, PASS, CONNECTING, ONLINE };
  int mode = LIST, sel = 0;
  std::vector<String> ssids;
  std::vector<int> rssi;
  String pass, ssid;
  uint32_t t0 = 0;
  WifiApp() { name = "WIFI"; }
  bool wantsAllKeys() override { return mode == PASS; }
  static String saved(const String& s) {  // stored password for an SSID
    File f = SD.open("/waveos/wifi.txt");
    if (!f) return String();
    String r;
    while (f.available()) {
      String l = f.readStringUntil('\n'); l.trim();
      if (l.startsWith("tz=")) continue;
      int t = l.indexOf('\t');
      if (t > 0 && l.substring(0, t) == s) r = l.substring(t + 1);
    }
    f.close();
    return r;
  }
  static void store(const String& s, const String& p) {
    std::vector<String> lines;
    File f = SD.open("/waveos/wifi.txt");
    if (f) { while (f.available()) { String l = f.readStringUntil('\n'); l.trim(); if (l.length() && !l.startsWith(s + "\t") && !l.startsWith("tz=")) lines.push_back(l); } f.close(); }
    lines.push_back(s + "\t" + p);
    f = SD.open("/waveos/wifi.txt", FILE_WRITE);
    if (!f) return;
    f.printf("tz=%d\n", Net::tz);
    for (auto& l : lines) f.println(l);
    f.close();
  }
  void loadTz() {
    File f = SD.open("/waveos/wifi.txt");
    if (!f) return;
    String l = f.readStringUntil('\n');
    if (l.startsWith("tz=")) Net::tz = l.substring(3).toInt();
    f.close();
  }
  void scan() {
    ssids.clear(); rssi.clear();
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n && ssids.size() < 20; i++) {
      String s = WiFi.SSID(i);
      if (!s.length() || std::find(ssids.begin(), ssids.end(), s) != ssids.end()) continue;
      ssids.push_back(s); rssi.push_back(WiFi.RSSI(i));
    }
    WiFi.scanDelete();
    sel = 0;
  }
  void enter() override {
    loadTz();
    if (Music::active() && !Music::paused()) Music::pause(true);
    if (ESP.getMaxAllocHeap() < 30000) { toast("NOT ENOUGH MEMORY FOR WIFI", 2000); }
    WiFi.mode(WIFI_STA);
    mode = LIST;
    toast("SCANNING...", 800);
    scan();
  }
  void leave() override {
    if (server) { server->stop(); delete server; server = nullptr; }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    mode = LIST;
  }
  void connect(const String& s, const String& p) {
    ssid = s; pass = p;
    WiFi.begin(s.c_str(), p.c_str());
    mode = CONNECTING; t0 = millis();
  }
  void online() {
    mode = ONLINE;
    store(ssid, pass);
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    if (!server) {
      server = new WebServer(80);
      server->on("/", HTTP_GET, handleList);
      server->on("/up", HTTP_POST, []() { server->sendHeader("Location", "/?d=" + urlEnc(server->hasArg("d") ? server->arg("d") : "/")); server->send(303); }, handleUpload);
      server->on("/dl", HTTP_GET, []() {
        File f = SD.open(server->arg("f"));
        if (!f || f.isDirectory()) { server->send(404, "text/plain", "not found"); return; }
        server->streamFile(f, "application/octet-stream"); f.close();
      });
      server->on("/rm", HTTP_GET, []() {
        String f = server->arg("f");
        SD.remove(f);
        String d = f.substring(0, f.lastIndexOf('/')); if (!d.length()) d = "/";
        server->sendHeader("Location", "/?d=" + urlEnc(d)); server->send(303);
      });
      server->on("/mk", HTTP_GET, []() {
        String d = server->arg("d"), n = server->arg("n");
        n.replace("/", "_");
        if (n.length()) SD.mkdir((d == "/" ? "" : d) + "/" + n);
        server->sendHeader("Location", "/?d=" + urlEnc(d)); server->send(303);
      });
      server->begin();
    }
    Audio::blip(1320, 120, Audio::W_SINE, 0.3f);
  }
  bool key(const KeyEv& e) override {
    if (mode == PASS) {
      if (e.code == K_ESC) { mode = LIST; return true; }
      if (e.code == K_ENTER) { connect(ssids[sel], pass); return true; }
      if (e.code == K_BACK) { if (pass.length()) pass.remove(pass.length() - 1); return true; }
      if (e.code == K_CHAR && !e.fn && pass.length() < 63) pass += e.ch;
      return true;
    }
    KeyCode k = e.nav();
    if (mode == ONLINE && e.code == K_CHAR && (e.ch == '-' || e.ch == '=')) { Net::tz = constrain(Net::tz + (e.ch == '=' ? 1 : -1), -12, 14); store(ssid, pass); return true; }
    if (mode == LIST) {
      int n = ssids.size();
      if (k == K_UP && n) { sel = (sel + n - 1) % n; return true; }
      if (k == K_DOWN && n) { sel = (sel + 1) % n; return true; }
      if (e.is('r')) { toast("SCANNING...", 800); scan(); return true; }
      if (k == K_ENTER && n) {
        String p = saved(ssids[sel]);
        if (p.length()) connect(ssids[sel], p);
        else { pass = ""; mode = PASS; }
        return true;
      }
      return false;
    }
    if (mode == CONNECTING && k == K_ESC) { WiFi.disconnect(); mode = LIST; return true; }
    if (mode == ONLINE && e.is('d')) { leave(); WiFi.mode(WIFI_STA); scan(); return true; }
    return false;
  }
  void tick() override {
    if (mode == CONNECTING) {
      if (WiFi.status() == WL_CONNECTED) online();
      else if (millis() - t0 > 15000) { WiFi.disconnect(); mode = LIST; toast("COULDN'T CONNECT - WRONG PASSWORD?", 2000); }
    }
    if (mode == ONLINE && server) server->handleClient();
  }
  void draw() override {
    background();
    statusBar("WIFI", T.cyan);
    char b[48];
    if (mode == LIST || mode == PASS) {
      panel(3, 14, 234, 109, T.edge);
      if (ssids.empty()) textC(120, 56, "NO NETWORKS - R TO RESCAN", T.grey);
      int top = std::max(0, sel - 7);
      for (int r = 0; r < 8 && top + r < (int)ssids.size(); r++) {
        int i = top + r, y = 18 + r * 12;
        bool s = i == sel;
        if (s) { cv.fillRect(5, y - 2, 230, 12, lerpc(T.panel, T.cyan, 0.2f)); cv.fillRect(5, y - 2, 2, 12, T.cyan); }
        int bars = rssi[i] > -55 ? 4 : rssi[i] > -67 ? 3 : rssi[i] > -78 ? 2 : 1;
        for (int k = 0; k < 4; k++) cv.fillRect(10 + k * 3, y + 6 - k * 2, 2, 2 + k * 2, k < bars ? T.green : T.dim);
        textFit(26, y, ssids[i].c_str(), s ? T.white : T.grey, 180, false);
        if (saved(ssids[i]).length()) text(214, y, "*", T.yel, 1, false);
      }
      if (mode == PASS) {
        panel(14, 40, 212, 46, T.cyan, T.bg1);
        textFit(20, 45, ("PASSWORD FOR " + ssids[sel]).c_str(), T.cyan, 200);
        String shown = pass + ((millis() / 400) % 2 ? "_" : " ");
        textFit(20, 62, shown.c_str(), T.white, 200, false);
        text(20, 75, "OK = CONNECT  ESC = BACK", T.dim, 1, false);
      }
      hintBar({{"^v", "NETWORK"}, {"OK", "CONNECT"}, {"R", "RESCAN"}, {"*", "SAVED"}});
    } else if (mode == CONNECTING) {
      textC(120, 50, ("CONNECTING TO " + ssid).c_str(), T.white);
      int dots = (millis() / 300) % 4;
      textC(120, 66, String("....").substring(0, dots).c_str(), T.cyan, 2);
      hintBar({{"ESC", "CANCEL"}});
    } else {
      String ip = WiFi.localIP().toString();
      textC(120, 18, ("CONNECTED: " + ssid).c_str(), T.green);
      panel(10, 32, 220, 34, T.cyan, T.bg1);
      textC(120, 36, "OPEN IN YOUR BROWSER:", T.grey, 1, false);
      textC(120, 48, ("http://" + ip).c_str(), T.white, 1);
      String c = Net::clock();
      snprintf(b, sizeof(b), "TIME %s  (UTC%+d)", c.length() ? c.c_str() : "SYNCING...", Net::tz);
      textC(120, 74, b, T.yel, 1, false);
      snprintf(b, sizeof(b), "%d FILES UPLOADED", uploads);
      textC(120, 86, b, T.cyan, 1, false);
      textC(120, 100, "KEEP THIS SCREEN OPEN WHILE COPYING", T.dim, 1, false);
      hintBar({{"- =", "TIME ZONE"}, {"D", "DISCONNECT"}, {"ESC", "WIFI OFF"}});
    }
  }
};

App* appWifi = new WifiApp();
