#include "web_ui.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "app.h"
#include "web_pages.h"

WebUi webUi;
static WebServer server(80);
static DNSServer dns;
static bool dns_running = false;

static bool auth_ok() {
  if (settings.web_pass.isEmpty()) return true;
  if (server.authenticate("admin", settings.web_pass.c_str())) return true;
  server.requestAuthentication();
  return false;
}

static void send_progmem(const char* type, const char* body) {
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, type, body);
}

static void handle_status() {
  JsonDocument doc;
  JsonObject vars = doc["vars"].to<JsonObject>();
  ups::State& st = upsHid.state();
  for (int i = 0;; ++i) {
    const ups::Var* v = st.at(i);
    if (!v) break;
    vars[v->name] = v->value;
  }
  doc["stale"] = st.stale();
  JsonObject w = doc["watcher"].to<JsonObject>();
  w["version"] = UPSW_VERSION;
  w["hostname"] = settings.hostname;
  w["ups_name"] = settings.ups_name;
  w["ip"] = app_ip();
  w["rssi"] = app.sta_connected ? WiFi.RSSI() : 0;
  w["ap_mode"] = app.ap_mode;
  w["ap_ssid"] = app.ap_ssid;
  w["ext_power"] = app.ext_power;
  w["ext_power_known"] = app.ext_power_known;
  w["core_batt"] = app.core_batt;
  w["usb_connected"] = upsHid.connected();
  w["sim"] = app.sim_active;
  w["polls"] = upsHid.poll_count();
  w["inputs"] = upsHid.input_count();
  w["nut_clients"] = nutServer.client_count();
  w["nut_logins"] = nutServer.login_count();
  w["uptime"] = (millis() - app.boot_ms) / 1000;
  w["heap"] = ESP.getFreeHeap();
  w["time"] = app_now_str();
  JsonArray ev = doc["events"].to<JsonArray>();
  for (int i = 0; i < events.count() && i < 12; ++i) {
    const Event& e = events.newest(i);
    ev.add(EventLog::format_time(e) + "  " + e.msg);
  }
  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "application/json", out);
}

static void handle_hid() {
  JsonDocument doc;
  doc["connected"] = upsHid.connected();
  char b[8];
  snprintf(b, sizeof b, "%04x", upsHid.vid()); doc["vid"] = b;
  snprintf(b, sizeof b, "%04x", upsHid.pid()); doc["pid"] = b;
  doc["mfr"] = upsHid.manufacturer();
  doc["product"] = upsHid.product();
  doc["serial"] = upsHid.serial();
  doc["cs"] = upsHid.cs_pin();
  doc["int"] = upsHid.int_pin();
  doc["desc_len"] = upsHid.desc_len();
  JsonArray reps = doc["reports"].to<JsonArray>();
  for (int i = 0; i < upsHid.report_count(); ++i) {
    const UpsHid::RawReport& r = upsHid.report(i);
    JsonObject o = reps.add<JsonObject>();
    o["id"] = r.id; o["len"] = r.len; o["ok"] = r.ok; o["fails"] = r.fails;
    o["age"] = r.ok ? (millis() - r.ms) / 1000 : 0;
    String raw;
    for (int k = 0; k < r.len; ++k) { char h[4]; snprintf(h, sizeof h, "%02X ", r.data[k]); raw += h; }
    o["raw"] = raw;
  }
  JsonArray fields = doc["fields"].to<JsonArray>();
  const hid::Descriptor& d = upsHid.descriptor();
  char path[160];
  for (int i = 0; i < d.size() && upsHid.have_descriptor(); ++i) {
    const hid::Field& f = d[i];
    JsonObject o = fields.add<JsonObject>();
    d.format_path(f, path, sizeof path);
    o["rid"] = f.report_id; o["t"] = String("IOF"[f.type]); o["off"] = f.bit_offset; o["bits"] = f.bit_size;
    o["lmin"] = f.log_min; o["lmax"] = f.log_max;
    snprintf(path + 150, 10, "%s", "");
    char u[12]; snprintf(u, sizeof u, "%08lX", (unsigned long)f.unit); o["unit"] = u; o["exp"] = f.unit_exp;
    o["path"] = path;
  }
  String raw;
  const uint8_t* db = upsHid.desc_bytes();
  for (int i = 0; i < upsHid.desc_len(); ++i) { char h[4]; snprintf(h, sizeof h, "%02X%s", db[i], (i & 15) == 15 ? "\n" : " "); raw += h; }
  doc["raw"] = raw;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handle_config_get() {
  if (!auth_ok()) return;
  JsonDocument doc;
  doc["wifi_ssid"] = settings.wifi_ssid;
  doc["hostname"] = settings.hostname;
  doc["ups_name"] = settings.ups_name;
  doc["ups_desc"] = settings.ups_desc;
  doc["nut_user"] = settings.nut_user;
  doc["poll_s"] = settings.poll_s;
  doc["disconnect_stale"] = settings.disconnect_stale ? 1 : 0;
  doc["disp_timeout_s"] = settings.disp_timeout_s;
  doc["brightness"] = settings.brightness;
  doc["tz"] = settings.tz;
  doc["ntp"] = settings.ntp;
  doc["sim"] = settings.sim ? 1 : 0;
  doc["usb_cs"] = settings.usb_cs;
  doc["usb_int"] = settings.usb_int;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handle_config_post() {
  if (!auth_ok()) return;
  bool reboot = false;
  auto s = [&](const char* k) { return server.hasArg(k) ? server.arg(k) : String(); };
  String ssid = s("wifi_ssid"), pass = s("wifi_pass"), host = s("hostname");
  host.trim();
  if (host.isEmpty()) host = "ups-watcher";
  if (ssid != settings.wifi_ssid || !pass.isEmpty() || host != settings.hostname) reboot = true;
  settings.wifi_ssid = ssid;
  if (!pass.isEmpty()) settings.wifi_pass = pass;
  settings.hostname = host;
  String un = s("ups_name"); un.trim(); if (un.isEmpty()) un = "ups";
  settings.ups_name = un;
  settings.ups_desc = s("ups_desc");
  settings.nut_user = s("nut_user");
  if (!s("nut_pass").isEmpty()) settings.nut_pass = s("nut_pass");
  if (settings.nut_user.isEmpty()) settings.nut_pass = "";
  int poll = s("poll_s").toInt(); settings.poll_s = poll < 2 ? 2 : poll > 60 ? 60 : poll;
  settings.disconnect_stale = s("disconnect_stale") == "1";
  settings.disp_timeout_s = s("disp_timeout_s").toInt();
  int br = s("brightness").toInt(); settings.brightness = br < 1 ? 1 : br > 255 ? 255 : br;
  if (!s("tz").isEmpty()) settings.tz = s("tz");
  if (!s("ntp").isEmpty()) settings.ntp = s("ntp");
  if (server.hasArg("web_pass") && !s("web_pass").isEmpty()) settings.web_pass = s("web_pass");
  settings.sim = s("sim") == "1";
  int cs = s("usb_cs").toInt(), in = s("usb_int").toInt();
  if (cs != settings.usb_cs || in != settings.usb_int) reboot = true;
  settings.usb_cs = cs; settings.usb_int = in;
  settings.save();
  events.add("Settings saved from web UI%s", reboot ? ", rebooting" : "");
  upsHid.set_poll_interval(settings.poll_s * 1000UL);
  server.sendHeader("Location", reboot ? "/" : "/config");
  server.send(303, "text/plain", reboot ? "Saved, rebooting" : "Saved");
  if (reboot) app_request_reboot(800);
}

static void handle_scan() {
  int n = WiFi.scanNetworks(false, false);
  JsonDocument doc;
  JsonArray a = doc.to<JsonArray>();
  for (int i = 0; i < n && i < 30; ++i) {
    JsonObject o = a.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handle_update_done() {
  bool ok = !Update.hasError();
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", ok ? "Update OK, rebooting" : "Update FAILED");
  events.add("Firmware update %s", ok ? "applied, rebooting" : "failed");
  if (ok) app_request_reboot(800);
}

static void handle_update_upload() {
  if (!settings.web_pass.isEmpty() && !server.authenticate("admin", settings.web_pass.c_str())) return;
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    Serial.printf("[ota] start %s\r\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.end(true)) Serial.printf("[ota] done, %u bytes\r\n", up.totalSize);
    else Update.printError(Serial);
  }
}

static bool captive_redirect() {
  if (!webUi.captive()) return false;
  String host = server.hostHeader();
  String ip = WiFi.softAPIP().toString();
  if (host == ip || host == settings.hostname + ".local") return false;
  server.sendHeader("Location", "http://" + ip + "/config", true);
  server.send(302, "text/plain", "");
  return true;
}

void WebUi::begin() {
  server.on("/style.css", HTTP_GET, []() { send_progmem("text/css", PAGE_CSS); });
  server.on("/", HTTP_GET, []() { if (captive_redirect()) return; send_progmem("text/html", PAGE_INDEX); });
  server.on("/config", HTTP_GET, []() { if (!auth_ok()) return; send_progmem("text/html", PAGE_CONFIG); });
  server.on("/update", HTTP_GET, []() { if (!auth_ok()) return; send_progmem("text/html", PAGE_UPDATE); });
  server.on("/hid", HTTP_GET, []() { send_progmem("text/html", PAGE_HID); });
  server.on("/api/status", HTTP_GET, handle_status);
  server.on("/api/hid", HTTP_GET, handle_hid);
  server.on("/api/config", HTTP_GET, handle_config_get);
  server.on("/api/config", HTTP_POST, handle_config_post);
  server.on("/api/scan", HTTP_GET, handle_scan);
  server.on("/api/instcmd", HTTP_POST, []() {
    if (!auth_ok()) return;
    String c = server.arg("cmd");
    bool ok = upsHid.state().has_cmd(c.c_str()) && upsHid.instcmd(c.c_str());
    server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "failed");
  });
  server.on("/api/sim", HTTP_POST, []() {
    if (!auth_ok()) return;
    app_sim_set(server.arg("mode").c_str());
    server.send(200, "text/plain", "OK");
  });
  server.on("/api/reboot", HTTP_POST, []() {
    if (!auth_ok()) return;
    server.send(200, "text/plain", "rebooting");
    events.add("Reboot requested from web UI");
    app_request_reboot(500);
  });
  server.on("/api/factory", HTTP_POST, []() {
    if (!auth_ok()) return;
    server.send(200, "text/plain", "erasing");
    app_factory_reset();
  });
  server.on("/update", HTTP_POST, handle_update_done, handle_update_upload);
  // Captive-portal probes
  const char* probes[] = {"/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
                          "/ncsi.txt", "/connecttest.txt", "/fwlink", "/success.txt", "/canonical.html"};
  for (auto p : probes) server.on(p, HTTP_GET, []() { if (!captive_redirect()) server.send(200, "text/plain", "ok"); });
  server.onNotFound([]() { if (!captive_redirect()) server.send(404, "text/plain", "not found"); });
  server.begin();
  Serial.println("[web] listening on 80");
}

void WebUi::set_captive(bool on) {
  captive_ = on;
  if (on && !dns_running) { dns.setErrorReplyCode(DNSReplyCode::NoError); dns.start(53, "*", WiFi.softAPIP()); dns_running = true; }
  if (!on && dns_running) { dns.stop(); dns_running = false; }
}

void WebUi::task() {
  if (dns_running) dns.processNextRequest();
  server.handleClient();
}
