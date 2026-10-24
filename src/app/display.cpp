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

void Display::draw_overview(bool full) {
  const char* st = v("ups.status", "");
  const char* word;
  uint16_t col;
  if (!upsHid.connected() && !app.sim_active) {
    word = app.ups_lost_ms ? "UPS LOST" : "NO UPS"; col = app.ups_lost_ms ? C_BAD : C_MUT;
  } else if (strstr(st, "OFF")) { word = "UPS LOST"; col = C_BAD; }
  else if (strstr(st, "LB")) { word = "LOW BATTERY"; col = C_BAD; }
  else if (strstr(st, "OB")) { word = "ON BATTERY"; col = C_WARN; }
  else if (strstr(st, "OL")) { word = "ONLINE"; col = C_OK; }
  else { word = "WAITING..."; col = C_MUT; }

  int y = HDR + 4;
  M5.Display.fillRect(0, y, W, 44, C_BG);
  M5.Display.setFont(&fonts::FreeSansBold18pt7b);
  M5.Display.setTextColor(col, C_BG);
  M5.Display.setTextDatum(textdatum_t::middle_left);
  M5.Display.drawString(word, 6, y + 22);
  // status tokens on the right
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextColor(C_MUT, C_BG);
  M5.Display.setTextDatum(textdatum_t::middle_right);
  M5.Display.drawString(st, W - 4, y + 22);

  // alarm / watcher power line
  y += 46;
  M5.Display.fillRect(0, y, W, 18, C_BG);
  M5.Display.setTextDatum(textdatum_t::middle_left);
  const char* al = upsHid.state().get("ups.alarm");
  if (al) { M5.Display.setTextColor(C_BAD, C_BG); M5.Display.drawString(al, 6, y + 9); }
  else if (app.ext_power_known && !app.ext_power) {
    M5.Display.setTextColor(C_WARN, C_BG);
    M5.Display.drawString(String("Watcher on internal battery ") + app.core_batt + "%", 6, y + 9);
  }

  // battery bar
  y += 22;
  int chg = atoi(v("battery.charge", "0"));
  M5.Display.fillRect(0, y, W, 40, C_BG);
  M5.Display.setFont(&fonts::Font4);
  M5.Display.setTextColor(C_FG, C_BG);
  M5.Display.setTextDatum(textdatum_t::middle_left);
  M5.Display.drawString(String("Battery ") + v("battery.charge") + "%", 6, y + 12);
  M5.Display.setTextDatum(textdatum_t::middle_right);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextColor(C_MUT, C_BG);
  M5.Display.drawString(String("runtime ") + runtime_str(), W - 6, y + 12);
  int bw = W - 12;
  M5.Display.drawRect(6, y + 26, bw, 10, C_LINE);
  uint16_t bc = chg > 50 ? C_OK : chg > 20 ? C_WARN : C_BAD;
  M5.Display.fillRect(7, y + 27, (bw - 2) * chg / 100, 8, bc);

  // metrics row
  y += 46;
  M5.Display.fillRect(0, y, W, H - FTR - y, C_BG);
  struct { const char* label; String val; } cells[3] = {
    {"Load", String(v("ups.load")) + "%"},
    {"Input", String(v("input.voltage")) + " V"},
    {"Batt V", String(v("battery.voltage")) + " V"},
  };
  const char* nom = upsHid.state().get("ups.realpower.nominal");
  if (nom && upsHid.state().has("ups.load")) {
    cells[0].val += " ~" + String(atoi(nom) * atoi(v("ups.load")) / 100) + "W";
  }
  for (int i = 0; i < 3; ++i) {
    int x = 6 + i * 106;
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextColor(C_MUT, C_BG);
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.drawString(cells[i].label, x, y);
    M5.Display.setFont(&fonts::Font4);
    M5.Display.setTextColor(C_FG, C_BG);
    M5.Display.drawString(cells[i].val, x, y + 16);
  }
  draw_footer("< Page", lit_ ? "Light" : "Light", "Page >");
}

void Display::draw_detail(bool full) {
  (void)full;
  static const char* rows[][2] = {
    {"Model", "ups.model"}, {"Serial", "ups.serial"}, {"Firmware", "ups.firmware"},
    {"Status", "ups.status"}, {"Charge", "battery.charge"}, {"Runtime", "battery.runtime"},
    {"Batt V", "battery.voltage"}, {"Batt nom", "battery.voltage.nominal"}, {"Low at", "battery.charge.low"},
    {"Input V", "input.voltage"}, {"Transfer", "input.transfer.low"}, {"Load %", "ups.load"},
    {"Nominal W", "ups.realpower.nominal"}, {"Beeper", "ups.beeper.status"}, {"Self test", "ups.test.result"},
    {"Batt date", "battery.date"}, {"Mfr date", "ups.mfr.date"}, {"Last xfer", "input.transfer.reason"},
  };
  int n = sizeof rows / sizeof rows[0];
  M5.Display.fillRect(0, HDR, W, H - HDR - FTR, C_BG);
  M5.Display.setFont(&fonts::Font2);
  int y = HDR + 2;
  for (int i = 0; i < n; ++i) {
    int col = i / 9, row = i % 9;
    int x = col * 160 + 4;
    int yy = y + row * 21;
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.setTextColor(C_MUT, C_BG);
    M5.Display.drawString(rows[i][0], x, yy);
    M5.Display.setTextColor(C_FG, C_BG);
    String val = v(rows[i][1]);
    if (strcmp(rows[i][1], "input.transfer.low") == 0) val += "-" + String(v("input.transfer.high"));
    if (strcmp(rows[i][1], "battery.runtime") == 0) val = runtime_str();
    if (val.length() > 13) val = val.substring(0, 13);
    M5.Display.setTextDatum(textdatum_t::top_right);
    M5.Display.drawString(val, x + 152, yy);
  }
  draw_footer("< Page", "Light", "Page >");
}

void Display::draw_events(bool full) {
  (void)full;
  M5.Display.fillRect(0, HDR, W, H - HDR - FTR, C_BG);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextDatum(textdatum_t::top_left);
  int y = HDR + 2;
  int shown = 0;
  for (int i = 0; i < events.count() && shown < 9; ++i, ++shown) {
    const Event& e = events.newest(i);
    String t = EventLog::format_time(e);
    if (e.ts) t = t.substring(5, 16);  // MM-DD HH:MM
    M5.Display.setTextColor(C_MUT, C_BG);
    M5.Display.drawString(t, 4, y + shown * 21);
    M5.Display.setTextColor(C_FG, C_BG);
    String m = e.msg;
    if (m.length() > 34) m = m.substring(0, 34);
    M5.Display.drawString(m, 92, y + shown * 21);
  }
  if (!events.count()) { M5.Display.setTextColor(C_MUT, C_BG); M5.Display.drawString("No events yet", 4, y); }
  draw_footer("< Page", "Light", "Page >");
}

void Display::draw_network(bool full) {
  (void)full;
  M5.Display.fillRect(0, HDR, W, H - HDR - FTR, C_BG);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextDatum(textdatum_t::top_left);
  String rows[][2] = {
    {"Wi-Fi", app.sta_connected ? WiFi.SSID() + " " + String(WiFi.RSSI()) + "dBm" : (app.ap_mode ? "AP " + app.ap_ssid : "connecting")},
    {"IP", app_ip()},
    {"Host", settings.hostname + ".local"},
    {"NUT", "port 3493, name '" + settings.ups_name + "'"},
    {"Clients", String(nutServer.client_count()) + " (" + nutServer.login_count() + " logged in)"},
    {"USB", upsHid.connected() ? "UPS on CS G" + String(upsHid.cs_pin()) + " INT G" + upsHid.int_pin() : "no device"},
    {"Power", app.ext_power_known ? (app.ext_power ? "external 5V" : "BATTERY " + String(app.core_batt) + "%") : "unknown"},
    {"Uptime", app_uptime_str((millis() - app.boot_ms) / 1000)},
    {"Time", app_now_str()},
    {"Version", UPSW_VERSION},
  };
  int n = sizeof rows / sizeof rows[0];
  for (int i = 0; i < n; ++i) {
    int yy = HDR + 2 + i * 19;
    M5.Display.setTextColor(C_MUT, C_BG);
    M5.Display.drawString(rows[i][0], 4, yy);
    M5.Display.setTextColor(C_FG, C_BG);
    M5.Display.drawString(rows[i][1], 70, yy);
  }
  draw_footer("< Page", "Light", "Page >");
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
    default: draw_network(full); break;
  }
  last_page_ = page_;
}
