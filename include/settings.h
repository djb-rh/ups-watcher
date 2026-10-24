// Persistent configuration (NVS via Preferences).
#pragma once
#include <Arduino.h>

struct Settings {
  String wifi_ssid, wifi_pass;
  String hostname = "ups-watcher";
  String ups_name = "ups";
  String ups_desc = "APC UPS";
  String nut_user, nut_pass;          // empty = open NUT server
  String web_pass;                    // empty = no password on the web UI
  String tz = "EST5EDT,M3.2.0,M11.1.0";
  String ntp = "pool.ntp.org";
  uint16_t poll_s = 5;                // full HID poll interval
  uint16_t disp_timeout_s = 300;      // backlight off after this long idle; 0 = never
  uint8_t brightness = 160;           // 0-255
  bool disconnect_stale = false;      // true: report DATA-STALE when the UPS vanishes; false: ALARM OFF
  bool sim = false;                   // simulated UPS when no real one is attached
  uint8_t usb_int = 35;               // MAX3421E INT GPIO (35 or 34), 0 = auto
  uint8_t usb_cs = 0;                 // MAX3421E CS GPIO (13, 5, 0=auto-scan)

  void load();
  void save() const;
  static void factory_reset();
};

extern Settings settings;
