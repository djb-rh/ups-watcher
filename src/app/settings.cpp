#include "settings.h"
#include <Preferences.h>

Settings settings;
static const char* NS = "upsw";

void Settings::load() {
  Preferences p;
  if (!p.begin(NS, true)) return;
  wifi_ssid = p.getString("ssid", wifi_ssid);
  wifi_pass = p.getString("pass", wifi_pass);
  hostname = p.getString("host", hostname);
  ups_name = p.getString("upsname", ups_name);
  ups_desc = p.getString("upsdesc", ups_desc);
  nut_user = p.getString("nutuser", nut_user);
  nut_pass = p.getString("nutpass", nut_pass);
  web_pass = p.getString("webpass", web_pass);
  tz = p.getString("tz", tz);
  ntp = p.getString("ntp", ntp);
  poll_s = p.getUShort("poll", poll_s);
  disp_timeout_s = p.getUShort("dispto", disp_timeout_s);
  brightness = p.getUChar("bright", brightness);
  disconnect_stale = p.getBool("dstale", disconnect_stale);
  sim = p.getBool("sim", sim);
  usb_int = p.getUChar("usbint", usb_int);
  usb_cs = p.getUChar("usbcs", usb_cs);
  p.end();
  if (poll_s < 2) poll_s = 2;
  if (hostname.isEmpty()) hostname = "ups-watcher";
  if (ups_name.isEmpty()) ups_name = "ups";
}

void Settings::save() const {
  Preferences p;
  if (!p.begin(NS, false)) return;
  p.putString("ssid", wifi_ssid);
  p.putString("pass", wifi_pass);
  p.putString("host", hostname);
  p.putString("upsname", ups_name);
  p.putString("upsdesc", ups_desc);
  p.putString("nutuser", nut_user);
  p.putString("nutpass", nut_pass);
  p.putString("webpass", web_pass);
  p.putString("tz", tz);
  p.putString("ntp", ntp);
  p.putUShort("poll", poll_s);
  p.putUShort("dispto", disp_timeout_s);
  p.putUChar("bright", brightness);
  p.putBool("dstale", disconnect_stale);
  p.putBool("sim", sim);
  p.putUChar("usbint", usb_int);
  p.putUChar("usbcs", usb_cs);
  p.end();
}

void Settings::factory_reset() {
  Preferences p;
  if (p.begin(NS, false)) { p.clear(); p.end(); }
}
