// UPS Watcher: a NUT server for the M5Stack Core Basic + USB Module, reading
// APC UPSes over USB HID. See README.md.
#include <Arduino.h>
#include <M5Unified.h>
#include <esp_system.h>
#include <esp_attr.h>
#include <esp_debug_helpers.h>
#include <rom/rtc.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <SPI.h>
#include <time.h>
#include "app.h"
#include "web_ui.h"
#include "display.h"
#include "ups_hid_map.h"

AppStatus app;
static uint32_t reboot_at = 0;
RTC_NOINIT_ATTR static uint32_t rtc_our_reboot;  // set by app_request_reboot, read at next boot
static uint32_t sta_started_ms = 0, last_sta_retry_ms = 0, ap_started_ms = 0;
static bool mdns_up = false, ntp_started = false;
static uint32_t last_flags = 0xFFFFFFFF;
static bool last_connected = false;
static String sim_mode = "ol";
static uint32_t sim_t0 = 0;

// ------------------------------------------------------------- helpers

String app_ip() {
  if (app.sta_connected) return WiFi.localIP().toString();
  if (app.ap_mode) return WiFi.softAPIP().toString();
  return "";
}

String app_uptime_str(uint32_t s) {
  char b[32];
  if (s >= 86400) snprintf(b, sizeof b, "%lud %luh %lum", (unsigned long)s / 86400, (unsigned long)(s % 86400) / 3600, (unsigned long)(s % 3600) / 60);
  else snprintf(b, sizeof b, "%luh %lum %lus", (unsigned long)s / 3600, (unsigned long)(s % 3600) / 60, (unsigned long)s % 60);
  return String(b);
}

const char* app_reset_reason() {
  static const char* reasons[] = {"unknown", "power on", "external reset", "software reset", "panic", "interrupt watchdog",
                                  "task watchdog", "other watchdog", "deep sleep wake", "brownout", "SDIO"};
  int rr = esp_reset_reason();
  return rr >= 0 && rr < (int)(sizeof reasons / sizeof reasons[0]) ? reasons[rr] : "unknown";
}

String app_now_str() {
  time_t now = time(nullptr);
  if (now < 1600000000) return "clock not set";
  struct tm tmv;
  localtime_r(&now, &tmv);
  char b[32];
  strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &tmv);
  return String(b);
}

void app_request_reboot(uint32_t delay_ms) { reboot_at = millis() + delay_ms; if (!reboot_at) reboot_at = 1; rtc_our_reboot = 1; }

void app_factory_reset() {
  events.add("Factory reset");
  Settings::factory_reset();
  events.clear();
  app_request_reboot(500);
}

// ------------------------------------------------------------- MAX3421E pins

// Reads the MAX3421E REVISION register with a bit-banged SPI on the given CS,
// before any driver has claimed the bus. 0x12/0x13 = chip present.
static uint8_t bb_xfer(uint8_t out) {
  uint8_t in = 0;
  for (int b = 7; b >= 0; --b) {
    digitalWrite(23, (out >> b) & 1);
    delayMicroseconds(2);
    digitalWrite(18, HIGH);
    delayMicroseconds(2);
    in = (in << 1) | digitalRead(19);
    digitalWrite(18, LOW);
    delayMicroseconds(2);
  }
  return in;
}

static int find_max3421_cs() {
  pinMode(18, OUTPUT); digitalWrite(18, LOW);
  pinMode(23, OUTPUT); digitalWrite(23, LOW);
  pinMode(19, INPUT);
  const int cands[] = {13, 5, 0};  // USB Module v1.2 "SS Select" CH1/CH2/CH3 on the Core
  int found = -1;
  for (int cs : cands) {
    pinMode(cs, OUTPUT); digitalWrite(cs, HIGH); delayMicroseconds(10);
    digitalWrite(cs, LOW); bb_xfer((17 << 3) | 0x02); bb_xfer(0x10); digitalWrite(cs, HIGH);  // PINCTL.FDUPSPI
    delayMicroseconds(10);
    digitalWrite(cs, LOW); bb_xfer(18 << 3); uint8_t rev = bb_xfer(0); digitalWrite(cs, HIGH);
    Serial.printf("[usb] CS G%d: REVISION 0x%02X\r\n", cs, rev);
    if (found < 0 && (rev == 0x12 || rev == 0x13)) found = cs;
    // keep every candidate driven high: a floating SS lets the chip listen to LCD traffic
  }
  pinMode(18, INPUT); pinMode(23, INPUT);
  return found;
}

// ------------------------------------------------------------- Wi-Fi

static void start_ap() {
  if (app.ap_mode) return;
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char ssid[32];
  snprintf(ssid, sizeof ssid, "UPS-Watcher-%02X%02X", mac[4], mac[5]);
  app.ap_ssid = ssid;
  WiFi.mode(settings.wifi_ssid.isEmpty() ? WIFI_AP : WIFI_AP_STA);
  WiFi.softAP(ssid);
  app.ap_mode = true;
  ap_started_ms = millis();
  webUi.set_captive(true);
  events.add("Setup AP %s up at %s", ssid, WiFi.softAPIP().toString().c_str());
}

static void stop_ap() {
  if (!app.ap_mode) return;
  webUi.set_captive(false);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  app.ap_mode = false;
  events.add("Setup AP closed");
}

static void start_sta() {
  if (settings.wifi_ssid.isEmpty()) return;
  WiFi.setHostname(settings.hostname.c_str());
  WiFi.begin(settings.wifi_ssid.c_str(), settings.wifi_pass.c_str());
  sta_started_ms = last_sta_retry_ms = millis();
}

static void on_wifi_event(WiFiEvent_t ev, WiFiEventInfo_t info) {
  (void)info;
  switch (ev) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      app.sta_connected = true;
      events.add("Wi-Fi connected, IP %s", WiFi.localIP().toString().c_str());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      if (app.sta_connected) events.add("Wi-Fi disconnected");
      app.sta_connected = false;
      mdns_up = false;
      break;
    default: break;
  }
}

static void wifi_task() {
  uint32_t now = millis();
  if (app.sta_connected) {
    if (!mdns_up) {
      MDNS.end();
      if (MDNS.begin(settings.hostname.c_str())) {
        MDNS.addService("nut", "tcp", 3493);
        MDNS.addService("http", "tcp", 80);
        mdns_up = true;
      }
    }
    if (!ntp_started) {
      configTzTime(settings.tz.c_str(), settings.ntp.c_str());
      ntp_started = true;
    }
    // Leave the setup AP up for a couple of minutes after connecting so the
    // screen can be read, then close it.
    if (app.ap_mode && !settings.wifi_ssid.isEmpty() && now - ap_started_ms > 120000) stop_ap();
    return;
  }
  if (settings.wifi_ssid.isEmpty()) { start_ap(); return; }
  if (!app.ap_mode && now - sta_started_ms > 30000) start_ap();
  if (now - last_sta_retry_ms > 60000) {
    last_sta_retry_ms = now;
    WiFi.disconnect();
    WiFi.begin(settings.wifi_ssid.c_str(), settings.wifi_pass.c_str());
  }
}

void app_wifi_reconfigure() { app_request_reboot(500); }

// ------------------------------------------------------------- Core power (IP5306)

static void power_task() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 1000) return;
  last = now;
  if (M5.Power.getType() != m5::Power_Class::pmic_ip5306) { app.ext_power_known = false; return; }
  // IP5306 REG_READ0 bit3: charging, REG_READ1 bit3: charge complete. Either
  // means 5 V is present on the input.
  uint8_t r0 = M5.In_I2C.readRegister8(0x75, 0x70, 400000);
  uint8_t r1 = M5.In_I2C.readRegister8(0x75, 0x71, 400000);
  bool ext = (r0 & 0x08) || (r1 & 0x08);
  app.core_batt = M5.Power.getBatteryLevel();
  app.core_full = r1 & 0x08;
  app.core_charging = (r0 & 0x08) && !app.core_full;
  if (!app.ext_power_known) { app.ext_power_known = true; app.ext_power = ext; return; }
  if (ext != app.ext_power) {
    app.ext_power = ext;
    if (!ext) {
      app.power_lost_ms = now; app.power_lost_time = time(nullptr);
      events.add("WATCHER LOST 5V INPUT (battery %d%%) - UPS output dead?", app.core_batt);
    } else {
      if (app.power_lost_ms) events.add("Watcher 5V input restored after %lus", (unsigned long)(now - app.power_lost_ms) / 1000);
      else events.add("Watcher 5V input restored (booted on battery %lus ago)", (unsigned long)(now - app.boot_ms) / 1000);
      app.power_lost_ms = 0;
    }
  }
  if (!ext && app.core_batt >= 0 && app.core_batt <= 10) {
    static uint32_t warned = 0;
    if (now - warned > 60000) { warned = now; events.add("Watcher battery critically low (%d%%)", app.core_batt); }
  }
}

// ------------------------------------------------------------- UPS supervision

static void refresh_alarm_text() {
  String a;
  if (app.ups_lost_ms && !settings.disconnect_stale) {
    a += "Lost contact with UPS";
    if (app.ups_lost_time > 1600000000) {
      struct tm tmv; localtime_r(&app.ups_lost_time, &tmv);
      char b[24]; strftime(b, sizeof b, " at %H:%M:%S", &tmv); a += b;
    }
  }
  if (app.ext_power_known && !app.ext_power) {
    if (a.length()) a += " ";
    a += "Watcher on internal battery (" + String(app.core_batt) + "%)";
  }
  upsHid.set_extra_alarm(a.c_str());
}

static void ups_task() {
  ups::State& st = upsHid.state();
  bool connected = upsHid.connected();
  if (connected != last_connected) {
    last_connected = connected;
    if (!connected && !app.sim_active) {
      app.ups_lost_ms = millis(); app.ups_lost_time = time(nullptr);
      if (settings.disconnect_stale) st.set_stale(true);
      else upsHid.set_offline(true);
      refresh_alarm_text();
    } else if (connected) {
      app.ups_lost_ms = 0;
      upsHid.set_offline(false);
      refresh_alarm_text();
    }
  }
  // Transitions in the status flags -> events
  uint32_t f = upsHid.flags() & ~upsmap::S_HAVE_CHG_FLAGS;
  if (upsHid.data_ok() && f != last_flags) {
    if (last_flags != 0xFFFFFFFF) {
      uint32_t ch = f ^ last_flags;
      if (ch & upsmap::S_ONLINE) {
        if (f & upsmap::S_ONLINE) events.add("Back on line power (input %s V)", st.get("input.voltage") ? st.get("input.voltage") : "?");
        else events.add("ON BATTERY - utility power lost (charge %s%%, %s)", st.get("battery.charge") ? st.get("battery.charge") : "?",
                        st.get("input.transfer.reason") ? st.get("input.transfer.reason") : "");
      }
      if ((ch & upsmap::S_LOWBATT) && (f & upsmap::S_LOWBATT)) events.add("LOW BATTERY (charge %s%%)", st.get("battery.charge") ? st.get("battery.charge") : "?");
      if ((ch & upsmap::S_SHUTDOWNIMM) && (f & upsmap::S_SHUTDOWNIMM)) events.add("UPS reports shutdown imminent");
      if ((ch & upsmap::S_REPLACEBATT) && (f & upsmap::S_REPLACEBATT)) events.add("UPS battery needs replacement");
      if ((ch & upsmap::S_OVERLOAD) && (f & upsmap::S_OVERLOAD)) events.add("UPS overload");
    }
    last_flags = f;
  }
  // watcher.* variables once a second
  static uint32_t last = 0;
  if (millis() - last >= 1000) {
    last = millis();
    st.set("watcher.power", app.ext_power_known ? (app.ext_power ? "external" : "battery") : "unknown", ups::F_STRING);
    if (app.core_batt >= 0) st.set_int("watcher.battery.charge", app.core_batt);
    st.set("watcher.battery.status", !app.ext_power_known ? "unknown" : !app.ext_power ? "discharging"
                                     : app.core_full ? "full" : app.core_charging ? "charging" : "idle", ups::F_STRING);
    st.set_int("watcher.uptime", (millis() - app.boot_ms) / 1000);
    if (upsHid.last_ok_ms()) st.set_int("watcher.ups.lastseen", (millis() - upsHid.last_ok_ms()) / 1000);
    static bool last_ext = true;
    if (app.ext_power_known && app.ext_power != last_ext) { last_ext = app.ext_power; refresh_alarm_text(); }
  }
}

// ------------------------------------------------------------- simulator

const char* app_sim_mode() { return sim_mode.c_str(); }

void app_sim_set(const char* mode) {
  sim_mode = mode;
  sim_t0 = millis();
  events.add("Simulator: %s", mode);
}

static void sim_task() {
  bool want = settings.sim && !upsHid.connected();
  if (want != app.sim_active) {
    app.sim_active = want;
    if (want) {
      events.add("Simulated UPS active (no real UPS on USB)");
      ups::State& st = upsHid.state();
      st.set("ups.mfr", "American Power Conversion", ups::F_STRING);
      st.set("ups.model", "Simulated Back-UPS 700", ups::F_STRING);
      st.set("ups.serial", "SIM0000001", ups::F_STRING);
      st.set("device.mfr", "American Power Conversion", ups::F_STRING);
      st.set("device.model", "Simulated Back-UPS 700", ups::F_STRING);
      st.set("battery.charge.low", "10", ups::F_RW);
      st.set("battery.runtime.low", "120", ups::F_RW);
      st.set("battery.voltage.nominal", "12.0");
      st.set("input.voltage.nominal", "120");
      st.set("input.transfer.low", "88", ups::F_RW);
      st.set("input.transfer.high", "139", ups::F_RW);
      st.set("ups.realpower.nominal", "420");
      st.set("ups.beeper.status", "enabled", ups::F_STRING);
      st.set("battery.type", "PbAc", ups::F_STRING);
      st.add_cmd("beeper.mute"); st.add_cmd("beeper.enable"); st.add_cmd("beeper.disable");
      app.ups_lost_ms = 0;
      upsHid.set_offline(false);
      refresh_alarm_text();
      sim_t0 = millis();
    } else {
      events.add("Simulated UPS stopped");
    }
  }
  if (!app.sim_active) return;
  static uint32_t last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  ups::State& st = upsHid.state();
  float t = (millis() - sim_t0) / 1000.0f;
  int charge = 100; float vin = 121.0f + 1.5f * sinf(t / 7); int load = 12 + (int)(3 * sinf(t / 11));
  long runtime = 3600 * 2;
  const char* status = "OL";
  if (sim_mode == "ob") { charge = 100 - (int)(t / 6); if (charge < 20) charge = 20; vin = 0; status = "OB DISCHRG"; runtime = charge * 60; }
  else if (sim_mode == "lb") { charge = 8; vin = 0; status = "OB DISCHRG LB"; runtime = 240; }
  else if (sim_mode == "off") { status = "ALARM OFF"; st.set("ups.alarm", "Simulated UPS lost", ups::F_STRING); }
  else { if (t < 60) status = "OL CHRG"; charge = t < 60 ? 90 + (int)(t / 6) : 100; }
  if (sim_mode != "off") st.remove("ups.alarm");
  st.set_int("battery.charge", charge);
  st.set_int("battery.runtime", runtime);
  st.set_num("battery.voltage", 12.3 + 0.01 * charge, 1);
  st.set_num("input.voltage", vin, 1);
  st.set_int("ups.load", load);
  st.set("ups.status", status, ups::F_STRING);
  st.set_stale(false);
}

// ------------------------------------------------------------- buttons

static void buttons_task() {
  if (M5.BtnA.wasClicked()) display.prev_page();
  if (M5.BtnC.wasClicked()) display.next_page();
  if (M5.BtnB.wasClicked()) display.toggle_light();
  // Hold A + C for 6 s: forget Wi-Fi and reopen the setup portal.
  static uint32_t hold_since = 0;
  if (M5.BtnA.isPressed() && M5.BtnC.isPressed()) {
    if (!hold_since) hold_since = millis();
    else if (millis() - hold_since > 6000) {
      hold_since = 0;
      events.add("Wi-Fi settings cleared from buttons");
      settings.wifi_ssid = ""; settings.wifi_pass = "";
      settings.save();
      app_request_reboot(300);
    }
  } else {
    hold_since = 0;
  }
}

// ------------------------------------------------------------- boot forensics
//
// RTC slow memory keeps its contents through any reset that leaves the 3.3 V
// rail up (EN pin, software, watchdog) but not through a real power loss. A
// marker there separates "something pulsed reset" from "the supply collapsed".
// A heartbeat in NVS every couple of minutes lets a cold start say roughly
// when the power went away.
static constexpr uint32_t RTC_MAGIC = 0x55505357;  // "UPSW"
RTC_NOINIT_ATTR static uint32_t rtc_magic;
RTC_NOINIT_ATTR static uint32_t rtc_uptime_s;
RTC_NOINIT_ATTR static uint32_t rtc_epoch;

// esp_restart() runs registered shutdown handlers first; esp_restart_noos()
// (panic, brownout, watchdog) does not. Recording who asked, from where, into
// RTC memory tells an explicit restart from everything else.
static constexpr uint32_t SD_MAGIC = 0x53445748;
RTC_NOINIT_ATTR static uint32_t rtc_sd_tag;
RTC_NOINIT_ATTR static uint32_t rtc_sd_uptime_s;
RTC_NOINIT_ATTR static char rtc_sd_task[16];
RTC_NOINIT_ATTR static uint32_t rtc_sd_bt[8];

static void shutdown_hook() {
  rtc_sd_tag = SD_MAGIC;
  rtc_sd_uptime_s = (millis() - app.boot_ms) / 1000;
  const char* tn = pcTaskGetName(NULL);
  strlcpy(rtc_sd_task, tn ? tn : "?", sizeof rtc_sd_task);
  esp_backtrace_frame_t f = {};
  esp_backtrace_get_start(&f.pc, &f.sp, &f.next_pc);
  for (int i = 0; i < 8; ++i) {
    rtc_sd_bt[i] = f.pc;
    if (!esp_backtrace_get_next_frame(&f)) { for (++i; i < 8; ++i) rtc_sd_bt[i] = 0; break; }
  }
}

static void heartbeat_task() {
  static uint32_t last = 0;
  uint32_t now = millis();
  rtc_uptime_s = (now - app.boot_ms) / 1000;
  time_t t = time(nullptr);
  if (t > 1600000000) rtc_epoch = (uint32_t)t;
  if (now - last < 120000 || t < 1600000000) return;
  last = now;
  Preferences p;
  if (p.begin("upsw", false)) { p.putUInt("hb", (uint32_t)t); p.end(); }
}

static String time_str(time_t t) {
  if (t < 1600000000) return "";
  struct tm tmv; localtime_r(&t, &tmv);
  char b[32]; strftime(b, sizeof b, "%Y-%m-%d %H:%M", &tmv);
  return String(b);
}

static void boot_forensics() {
  bool warm = rtc_magic == RTC_MAGIC;
  uint32_t prev_up = warm ? rtc_uptime_s : 0, prev_epoch = warm ? rtc_epoch : 0;
  rtc_magic = RTC_MAGIC; rtc_uptime_s = 0; rtc_epoch = 0;
  uint32_t hb = 0;
  Preferences p;
  if (p.begin("upsw", true)) { hb = p.getUInt("hb", 0); p.end(); }
  const char* why = app_reset_reason();
  int raw0 = rtc_get_reset_reason(0), raw1 = rtc_get_reset_reason(1);
  if (warm && rtc_sd_tag == SD_MAGIC) {
    events.add("Last run: esp_restart() from '%s' at %s, %s", rtc_sd_task,
               app_uptime_str(rtc_sd_uptime_s).c_str(), rtc_our_reboot ? "ours" : "NOT ours");
    String bt;
    for (int i = 0; i < 6 && rtc_sd_bt[i]; ++i) { char b[12]; snprintf(b, sizeof b, " %08lx", (unsigned long)rtc_sd_bt[i]); bt += b; }
    if (!rtc_our_reboot) events.add("Restart backtrace:%s", bt.c_str());
  }
  events.add("Raw reset cause cpu0=%d cpu1=%d, RTC memory %s", raw0, raw1, warm ? "intact" : "lost");
  if (esp_reset_reason() == ESP_RST_POWERON) {
    if (warm) {
      app.boot_kind = "EN pin reset while powered (3.3 V held)";
      events.add("Boot %s: EN-pin reset, 3.3 V held; prev uptime %s%s", UPSW_VERSION, app_uptime_str(prev_up).c_str(),
                 prev_epoch ? (", alive " + time_str(prev_epoch)).c_str() : "");
    } else {
      app.boot_kind = "cold start (power was lost)";
      events.add("Boot %s: cold start, power was lost%s", UPSW_VERSION, hb ? ("; last heartbeat " + time_str(hb)).c_str() : "");
    }
  } else if (esp_reset_reason() == ESP_RST_BROWNOUT || (esp_reset_reason() == ESP_RST_SW && warm && rtc_sd_tag != SD_MAGIC && !rtc_our_reboot)) {
    // A software reset nobody asked for, with RTC memory intact, is the
    // brownout interrupt (the Arduino core runs the detector in ISR mode and
    // the reason hint does not always survive the sag).
    app.boot_kind = "brownout (3.3 V sagged)";
    events.add("Boot %s: brownout, supply sagged; prev uptime %s", UPSW_VERSION, app_uptime_str(prev_up).c_str());
  } else {
    app.boot_kind = why;
    events.add("Boot %s: %s%s", UPSW_VERSION, why, warm && prev_up ? ("; prev uptime " + app_uptime_str(prev_up)).c_str() : "");
  }
  rtc_sd_tag = 0; rtc_our_reboot = 0;
}

// ------------------------------------------------------------- setup / loop

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\r\nUPS Watcher " UPSW_VERSION);
  app.boot_ms = millis();
  settings.load();

  // Find the USB Module before any SPI driver touches the bus.
  int cs = settings.usb_cs ? settings.usb_cs : find_max3421_cs();

  auto cfg = M5.config();
  cfg.internal_spk = false;  // the Core Basic amp hums; keep its input quiet
  cfg.internal_mic = false;
  M5.begin(cfg);
  M5.Speaker.end();
  pinMode(25, OUTPUT); digitalWrite(25, LOW);
  M5.Display.setBrightness(settings.brightness);

  events.begin();
  boot_forensics();
  esp_register_shutdown_handler(shutdown_hook);
  if (M5.Power.getType() == m5::Power_Class::pmic_ip5306) {
    // Button behaviour. The red side button is the IP5306 KEY input as well as
    // the ESP32 reset. SYS_CTL1 bit7 picks the power-off gesture (0 = hold
    // 2 s, 1 = double-click) and bit6 the flashlight gesture (0 = double-click,
    // 1 = hold). M5Unified leaves both at 0, so a double-click toggled the
    // (absent) flashlight output and never powered down. Swap them: double-
    // click powers off, the hold goes to the disabled WLED. SYS_CTL0 bit0 must
    // stay set for KEY power-off to work at all; bit1 ("boost output normally
    // open") is left clear - it was tried against the unplug reboot and did
    // nothing. None of this can cut power while the USB-C input is present.
    {
      uint8_t c0 = M5.In_I2C.readRegister8(0x75, 0x00, 400000);
      uint8_t c1 = M5.In_I2C.readRegister8(0x75, 0x01, 400000);
      M5.In_I2C.writeRegister8(0x75, 0x00, (c0 | 0x01) & ~0x02, 400000);
      M5.In_I2C.writeRegister8(0x75, 0x01, c1 | 0xC0, 400000);
    }
    for (int i = 0; i < 3; ++i) app.ip5306_ctl[i] = M5.In_I2C.readRegister8(0x75, i, 400000);
    Serial.printf("[power] IP5306 SYS_CTL0..2 = %02x %02x %02x\r\n", app.ip5306_ctl[0], app.ip5306_ctl[1], app.ip5306_ctl[2]);
  }
  display.begin();

  upsHid.on_event = [](const char* m) { events.add("%s", m); };
  upsHid.set_poll_interval(settings.poll_s * 1000UL);
  if (cs < 0) {
    events.add("USB Module (MAX3421E) not found - check it is seated and its DIP switches");
  } else {
    upsHid.begin(cs, settings.usb_int ? settings.usb_int : 35);
  }

  WiFi.onEvent(on_wifi_event);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  if (settings.wifi_ssid.isEmpty()) {
    WiFi.mode(WIFI_AP);
    start_ap();
  } else {
    WiFi.mode(WIFI_STA);
    start_sta();
  }
  webUi.begin();
  nutServer.begin(3493);
  Serial.println("[app] ready");
}

void loop() {
  M5.update();
  upsHid.task();
  wifi_task();
  power_task();
  heartbeat_task();
  ups_task();
  sim_task();

  nut::Config cfg;
  cfg.ups_name = settings.ups_name.c_str();
  cfg.ups_desc = settings.ups_desc.c_str();
  cfg.username = settings.nut_user.c_str();
  cfg.password = settings.nut_pass.c_str();
  cfg.version = UPSW_VERSION;
  nut::Hooks hooks;
  hooks.instcmd = [](const char* c, const char*) { return app.sim_active ? true : upsHid.instcmd(c); };
  hooks.setvar = [](const char* v, const char* val) {
    if (app.sim_active) { upsHid.state().set(v, val, ups::F_RW); return true; }
    return upsHid.setvar(v, val);
  };
  nutServer.task(upsHid.state(), cfg, hooks);
  webUi.task();
  buttons_task();
  display.task();

  if (reboot_at && (int32_t)(millis() - reboot_at) >= 0) {
    Serial.println("[app] rebooting");
    delay(100);
    ESP.restart();
  }
  delay(1);
}
