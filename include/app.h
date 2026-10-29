// Shared application state between main, web UI and display.
#pragma once
#include <Arduino.h>
#include "settings.h"
#include "events.h"
#include "ups_hid.h"
#include "nut_server.h"

struct AppStatus {
  bool ap_mode = false;        // captive portal AP is up
  bool sta_connected = false;
  String ap_ssid;
  bool ext_power = true;       // Core has 5 V in
  bool ext_power_known = false;
  int core_batt = -1;          // IP5306 level (0/25/50/75/100), -1 unknown
  bool core_charging = false;  // IP5306 says current is going into the cell
  bool core_full = false;      // IP5306 says the cell is full
  uint8_t ip5306_ctl[3] = {0, 0, 0};  // SYS_CTL0..2 as read after our setup
  uint32_t ups_lost_ms = 0;    // millis() when the UPS vanished (0 = not lost)
  time_t ups_lost_time = 0;
  uint32_t power_lost_ms = 0;
  time_t power_lost_time = 0;
  bool sim_active = false;
  uint32_t boot_ms = 0;
  String boot_kind;            // how this boot came about, for the status page
};

extern AppStatus app;

void app_request_reboot(uint32_t delay_ms = 500);
bool app_request_power_off(const char* who);  // false = refused (5 V input present)
void app_factory_reset();
void app_sim_set(const char* mode);  // "ol", "ob", "lb", "off", "none"
const char* app_sim_mode();
void app_wifi_reconfigure();          // after settings changed
String app_ip();
String app_uptime_str(uint32_t secs);
String app_now_str();
const char* app_reset_reason();
