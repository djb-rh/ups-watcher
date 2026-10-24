#include "display.h"
#include <M5Unified.h>
#include <WiFi.h>
#include "app.h"
#include "ups_hid_map.h"

Display display;

static constexpr int W = 320, H = 240;
static constexpr int HDR = 22, FTR = 20;
static const uint16_t C_BG = TFT_BLACK, C_FG = TFT_WHITE, C_MUT = 0x8C71, C_OK = 0x2E8B, C_WARN = 0xFD20,
                      C_BAD = 0xE124, C_LINE = 0x2124, C_ACC = 0x4D1F;

static const char* v(const char* name, const char* dflt = "-") {
  const char* s = upsHid.state().get(name);
  return s ? s : dflt;
}

static String runtime_str() {
  const char* s = upsHid.state().get("battery.runtime");
  if (!s) return "-";
  long sec = atol(s);
  char b[24];
  if (sec >= 3600) snprintf(b, sizeof b, "%ldh %02ldm", sec / 3600, (sec % 3600) / 60);
  else snprintf(b, sizeof b, "%ldm %02lds", sec / 60, sec % 60);
  return String(b);
}

static bool alarm_now() {
  const char* st = upsHid.state().get("ups.status");
  if (!st) return false;
  return strstr(st, "OB") || strstr(st, "LB") || strstr(st, "OFF") || strstr(st, "ALARM");
}

void Display::begin() {
  M5.Display.setRotation(1);
  M5.Display.setBrightness(settings.brightness);
  M5.Display.fillScreen(C_BG);
  M5.Display.setTextWrap(false);
  last_activity_ms_ = millis();
  lit_ = true;
}

void Display::wake() {
  last_activity_ms_ = millis();
  if (!lit_) { lit_ = true; M5.Display.setBrightness(settings.brightness); }
}

void Display::toggle_light() {
  last_activity_ms_ = millis();
  lit_ = !lit_;
  M5.Display.setBrightness(lit_ ? settings.brightness : 0);
}

void Display::next_page() { wake(); page_ = (page_ + 1) % 4; }
void Display::prev_page() { wake(); page_ = (page_ + 3) % 4; }

void Display::draw_header(bool full) {
  String right;
  if (app.ap_mode) right = "AP " + app.ap_ssid;
  else if (app.sta_connected) right = app_ip();
  else right = "no wifi";
  String left = v("ups.model", upsHid.connected() ? "UPS" : (app.sim_active ? "SIMULATED UPS" : "No UPS"));
  String hdr = left + "|" + right;
  if (!full && hdr == last_header_) return;
  last_header_ = hdr;
  M5.Display.fillRect(0, 0, W, HDR, 0x10A2);
  M5.Display.setTextColor(C_FG, 0x10A2);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextDatum(textdatum_t::middle_left);
  M5.Display.drawString(left, 4, HDR / 2);
  M5.Display.setTextDatum(textdatum_t::middle_right);
  M5.Display.setTextColor(app.ap_mode ? C_WARN : C_MUT, 0x10A2);
  M5.Display.drawString(right, W - 4, HDR / 2);
}

void Display::draw_footer(const char* a, const char* b, const char* c) {
  M5.Display.fillRect(0, H - FTR, W, FTR, C_BG);
  M5.Display.drawFastHLine(0, H - FTR, W, C_LINE);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextColor(C_MUT, C_BG);
  M5.Display.setTextDatum(textdatum_t::middle_center);
  M5.Display.drawString(a, 65, H - FTR / 2);
  M5.Display.drawString(b, 160, H - FTR / 2);
  M5.Display.drawString(c, 255, H - FTR / 2);
}

// Trim a string (with "..") until it fits in w pixels of the given font.
static String fit(String s, int w, const lgfx::IFont* font) {
  M5.Display.setFont(font);
  if (M5.Display.textWidth(s) <= w) return s;
  while (s.length() > 1 && M5.Display.textWidth(s + "..") > w) s.remove(s.length() - 1);
  return s + "..";
}

// Text fields are drawn with setTextPadding so each repaint overwrites the
// old pixels in one pass instead of clearing the area first (no flicker).
static void field(int x, int y, int w, const String& txt, const lgfx::IFont* font, uint16_t fg,
                  textdatum_t datum = textdatum_t::top_left) {
  M5.Display.setFont(font);
  M5.Display.setTextColor(fg, C_BG);
  M5.Display.setTextDatum(datum);
  M5.Display.setTextPadding(w);
  M5.Display.drawString(txt, x, y);
  M5.Display.setTextPadding(0);
}

void Display::draw_overview(bool full) {
  const char* st = v("ups.status", "");
  const char* word;
  uint16_t col;
  if (!upsHid.connected() && !app.sim_active) {
    word = app.ups_lost_ms ? "UPS LOST" : "NO UPS"; col = app.ups_lost_ms ? C_BAD : C_MUT;
  } else if (upsHid.connected() && !upsHid.data_ok()) { word = "READING..."; col = C_MUT; }
  else if (strstr(st, "OFF")) { word = "UPS LOST"; col = C_BAD; }
  else if (strstr(st, "LB")) { word = "LOW BATTERY"; col = C_BAD; }
  else if (strstr(st, "OB")) { word = "ON BATTERY"; col = C_WARN; }
  else if (strstr(st, "OL")) { word = "ONLINE"; col = C_OK; }
  else { word = "WAITING..."; col = C_MUT; }

  int y = HDR + 4;
  field(6, y + 22, W - 12, word, &fonts::FreeSansBold18pt7b, col, textdatum_t::middle_left);

  // line 2: status tokens + UPS alarm text (the Watcher's own power is line 3)
  y += 46;
  String al = upsHid.state().get("ups.alarm") ? upsHid.state().get("ups.alarm") : "";
  int wp = al.indexOf("Watcher on internal battery");
  if (wp >= 0) al = al.substring(0, wp);
  al.trim();
  String line2 = st;
  uint16_t c2 = C_MUT;
  if (al.length()) { line2 += "  " + al; c2 = C_BAD; }
  field(6, y + 9, W - 6, fit(line2, W - 8, &fonts::Font2), &fonts::Font2, c2, textdatum_t::middle_left);

  // line 3: the Watcher running on its own cell
  y += 18;
  String line3;
  if (app.ext_power_known && !app.ext_power) {
    line3 = "Watcher on its own battery " + String(app.core_batt) + "%";
    if (app.power_lost_ms) line3 += " for " + app_uptime_str((millis() - app.power_lost_ms) / 1000);
  }
  field(6, y + 9, W - 6, fit(line3, W - 8, &fonts::Font2), &fonts::Font2, C_WARN, textdatum_t::middle_left);

  // battery
  y += 22;
  int chg = atoi(v("battery.charge", "0"));
  field(6, y + 12, 170, String("Battery ") + v("battery.charge") + "%", &fonts::Font4, C_FG, textdatum_t::middle_left);
  field(W - 6, y + 12, 136, String("runtime ") + runtime_str(), &fonts::Font2, C_MUT, textdatum_t::middle_right);
  static int last_chg = -1;
  if (full || chg != last_chg) {
    last_chg = chg;
    int bw = W - 12;
    M5.Display.drawRect(6, y + 26, bw, 10, C_LINE);
    uint16_t bc = chg > 50 ? C_OK : chg > 20 ? C_WARN : C_BAD;
    int fillw = (bw - 2) * (chg < 0 ? 0 : chg > 100 ? 100 : chg) / 100;
    M5.Display.fillRect(7, y + 27, fillw, 8, bc);
    M5.Display.fillRect(7 + fillw, y + 27, bw - 2 - fillw, 8, C_BG);
  }

  // metrics row
  y += 46;
  struct { const char* label; String val; } cells[3] = {
    {"Load", String(v("ups.load")) + "%"},
    {"Input", String(v("input.voltage")) + " V"},
    {"Batt V", String(v("battery.voltage")) + " V"},
  };
  const char* nom = upsHid.state().get("ups.realpower.nominal");
  if (nom && upsHid.state().has("ups.load")) cells[0].val += " ~" + String(atoi(nom) * atoi(v("ups.load")) / 100) + "W";
  for (int i = 0; i < 3; ++i) {
    int x = 6 + i * 106;
    field(x, y, 100, cells[i].label, &fonts::Font2, C_MUT);
    field(x, y + 16, 100, cells[i].val, &fonts::Font4, C_FG);
  }
  if (full) draw_footer("< Page", "Light", "Page >");
}

void Display::draw_detail(bool full) {
  static const char* rows[][2] = {
    {"Model", "ups.model"}, {"Serial", "ups.serial"}, {"Firmware", "ups.firmware"},
    {"Status", "ups.status"}, {"Charge", "battery.charge"}, {"Runtime", "battery.runtime"},
    {"Batt V", "battery.voltage"}, {"Batt nom", "battery.voltage.nominal"}, {"Low at", "battery.charge.low"},
    {"Input V", "input.voltage"}, {"Transfer", "input.transfer.low"}, {"Load %", "ups.load"},
    {"Nominal W", "ups.realpower.nominal"}, {"Beeper", "ups.beeper.status"}, {"Self test", "ups.test.result"},
    {"Batt date", "battery.date"}, {"Mfr date", "ups.mfr.date"}, {"Last xfer", "input.transfer.reason"},
  };
  int n = sizeof rows / sizeof rows[0];
  int y = HDR + 2;
  for (int i = 0; i < n; ++i) {
    int col = i / 9, row = i % 9;
    int x = col * 160 + 4;
    int yy = y + row * 21;
    if (full) field(x, yy, 60, rows[i][0], &fonts::Font2, C_MUT);
    String val = v(rows[i][1]);
    if (strcmp(rows[i][1], "input.transfer.low") == 0) val += "-" + String(v("input.transfer.high"));
    if (strcmp(rows[i][1], "battery.runtime") == 0) val = runtime_str();
    if (val.length() > 13) val = val.substring(0, 13);
    field(x + 152, yy, 92, val, &fonts::Font2, C_FG, textdatum_t::top_right);
  }
  if (full) draw_footer("< Page", "Light", "Page >");
}

void Display::draw_events(bool full) {
  int y = HDR + 2;
  int shown = 0;
  for (int i = 0; i < events.count() && shown < 9; ++i, ++shown) {
    const Event& e = events.newest(i);
    String t = EventLog::format_time(e);
    if (e.ts) t = t.substring(5, 16);  // MM-DD HH:MM
    field(4, y + shown * 21, 86, t, &fonts::Font2, C_MUT);
    String m = e.msg;
    if (m.length() > 34) m = m.substring(0, 34);
    field(92, y + shown * 21, W - 94, m, &fonts::Font2, C_FG);
  }
  if (!events.count()) field(4, y, 200, "No events yet", &fonts::Font2, C_MUT);
  if (full) draw_footer("< Page", "Light", "Page >");
}

static String ago_str(uint32_t ms) {
  if (!ms) return "never";
  uint32_t s = (millis() - ms) / 1000;
  if (s < 60) return String(s) + "s ago";
  if (s < 3600) return String(s / 60) + "m " + String(s % 60) + "s ago";
  return app_uptime_str(s) + " ago";
}

static String clock_str(time_t t) {
  if (t < 1600000000) return "";
  struct tm tmv; localtime_r(&t, &tmv);
  char b[24]; strftime(b, sizeof b, "%H:%M:%S", &tmv);
  return String(b);
}

// Page 4: the Watcher itself. Power / internal battery on top, then the
// network, NUT server, USB host and system rows.
void Display::draw_watcher(bool full) {
  int y = HDR + 4;
  String word; uint16_t col;
  if (!app.ext_power_known) { word = "Power: unknown"; col = C_MUT; }
  else if (!app.ext_power) { word = "ON INTERNAL BATTERY"; col = C_BAD; }
  else { word = "5V input OK"; col = C_OK; }
  field(6, y + 10, 196, word, &fonts::Font4, col, textdatum_t::middle_left);

  String bt;
  int chg = app.core_batt;
  if (!app.ext_power_known || chg < 0) bt = "battery ?";
  else if (app.core_full) bt = "battery full";
  else if (app.core_charging) bt = "charging " + String(chg) + "%";
  else if (!app.ext_power) bt = "battery " + String(chg) + "%";
  else if (chg == 0) bt = "0% - switch off?";
  else bt = "battery " + String(chg) + "%";
  field(W - 6, y + 10, 112, bt, &fonts::Font2, app.ext_power ? C_MUT : C_WARN, textdatum_t::middle_right);

  // battery bar (coarse: the IP5306 reports 0/25/50/75/100)
  y += 24;
  static int last_chg = -2;
  static bool last_ext = false;
  if (full || chg != last_chg || app.ext_power != last_ext) {
    last_chg = chg; last_ext = app.ext_power;
    int bw = W - 12;
    M5.Display.drawRect(6, y, bw, 8, C_LINE);
    int c = chg < 0 ? 0 : chg > 100 ? 100 : chg;
    uint16_t bc = app.core_full ? C_OK : c > 50 ? C_OK : c > 25 ? C_WARN : C_BAD;
    if (app.core_full) c = 100;
    int fillw = (bw - 2) * c / 100;
    M5.Display.fillRect(7, y + 1, fillw, 6, bc);
    M5.Display.fillRect(7 + fillw, y + 1, bw - 2 - fillw, 6, C_BG);
  }

  y += 14;
  String wifi;
  if (app.sta_connected) {
    int r = WiFi.RSSI();
    const char* q = r > -60 ? "good" : r > -70 ? "ok" : r > -80 ? "weak" : "poor";
    wifi = WiFi.SSID() + " " + String(r) + "dBm " + q + " ch" + String(WiFi.channel());
  } else if (app.ap_mode) wifi = "AP " + app.ap_ssid + " (setup mode)";
  else wifi = "connecting to " + settings.wifi_ssid;
  String usb;
  if (upsHid.connected()) usb = String(upsHid.product()[0] ? upsHid.product() : "UPS") + " CS G" + upsHid.cs_pin() + " INT G" + upsHid.int_pin();
  else if (app.sim_active) usb = "simulated UPS";
  else usb = "no device on CS G" + String(upsHid.cs_pin()) + " INT G" + upsHid.int_pin();
  String seen = upsHid.connected() ? ago_str(upsHid.last_ok_ms()) + " (" + String(upsHid.poll_count()) + " polls)" : ago_str(upsHid.last_ok_ms());
  String tm = app_now_str();
  String ver = UPSW_VERSION;
  if (app.power_lost_time > 1600000000) ver += "   5V lost " + clock_str(app.power_lost_time);
  String rows[][2] = {
    {"Wi-Fi", wifi},
    {"IP", app_ip() + "  " + settings.hostname + ".local"},
    {"NUT", "port 3493 '" + settings.ups_name + "'  " + String(nutServer.client_count()) + " clients, " + nutServer.login_count() + " logged in"},
    {"USB", usb},
    {"UPS seen", seen},
    {"Uptime", app_uptime_str((millis() - app.boot_ms) / 1000) + "  " + String(ESP.getFreeHeap() / 1024) + " KB free"},
    {"Time", tm},
    {"Version", ver},
  };
  int n = sizeof rows / sizeof rows[0];
  for (int i = 0; i < n; ++i) {
    int yy = y + i * 18;
    if (full) field(4, yy, 64, rows[i][0], &fonts::Font2, C_MUT);
    field(70, yy, W - 72, rows[i][1], &fonts::Font2, C_FG);
  }
  if (full) draw_footer("< Page", "Light", "Page >");
}

void Display::task() {
  uint32_t now = millis();
  bool alarm = alarm_now() || (app.ext_power_known && !app.ext_power);
  if (alarm && !last_alarm_) wake();  // alarms turn the screen on
  last_alarm_ = alarm;
  if (lit_ && settings.disp_timeout_s && !alarm && now - last_activity_ms_ > settings.disp_timeout_s * 1000UL) {
    lit_ = false;
    M5.Display.setBrightness(0);
  }
  bool page_changed = page_ != last_page_;
  uint32_t gen = upsHid.state().generation();
  bool periodic = now - last_draw_ms_ >= 1000;
  if (!page_changed && gen == last_gen_ && !periodic) return;
  if (!lit_ && !page_changed) { last_gen_ = gen; last_draw_ms_ = now; return; }  // nothing to see
  last_gen_ = gen; last_draw_ms_ = now;
  bool full = page_changed;
  if (full) M5.Display.fillScreen(C_BG);
  draw_header(full);
  switch (page_) {
    case 0: draw_overview(full); break;
    case 1: draw_detail(full); break;
    case 2: draw_events(full); break;
    default: draw_watcher(full); break;
  }
  last_page_ = page_;
}
