#include "events.h"
#include <LittleFS.h>
#include <stdarg.h>

EventLog events;
static const char* PATH = "/events.log";
static const size_t MAX_BYTES = 24 * 1024;

void EventLog::begin() {
  fs_ok_ = LittleFS.begin(true);
  if (!fs_ok_) Serial.println("[events] LittleFS unavailable, log is RAM only");
  load();
}

void EventLog::add(const char* fmt, ...) {
  Event& e = ring_[head_];
  head_ = (head_ + 1) % CAP;
  if (n_ < CAP) n_++;
  time_t now = time(nullptr);
  e.ts = now > 1600000000 ? now : 0;
  e.uptime_s = millis() / 1000;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(e.msg, sizeof e.msg, fmt, ap);
  va_end(ap);
  Serial.printf("[event] %s %s\r\n", format_time(e).c_str(), e.msg);
  persist(e);
}

String EventLog::format_time(const Event& e) {
  char buf[32];
  if (e.ts) {
    struct tm tmv;
    localtime_r(&e.ts, &tmv);
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tmv);
  } else {
    snprintf(buf, sizeof buf, "up %lus", (unsigned long)e.uptime_s);
  }
  return String(buf);
}

void EventLog::persist(const Event& e) {
  if (!fs_ok_) return;
  File f = LittleFS.open(PATH, "a");
  if (!f) return;
  if (f.size() > MAX_BYTES) {
    f.close();
    // Keep the newest half: rewrite from RAM.
    f = LittleFS.open(PATH, "w");
    if (!f) return;
    for (int i = n_ - 1; i >= 0; --i) {
      const Event& x = newest(i);
      f.printf("%ld\t%lu\t%s\n", (long)x.ts, (unsigned long)x.uptime_s, x.msg);
    }
    f.close();
    return;
  }
  f.printf("%ld\t%lu\t%s\n", (long)e.ts, (unsigned long)e.uptime_s, e.msg);
  f.close();
}

void EventLog::load() {
  if (!fs_ok_) return;
  File f = LittleFS.open(PATH, "r");
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    int t1 = line.indexOf('\t'), t2 = line.indexOf('\t', t1 + 1);
    if (t1 < 0 || t2 < 0) continue;
    Event& e = ring_[head_];
    head_ = (head_ + 1) % CAP;
    if (n_ < CAP) n_++;
    e.ts = (time_t)line.substring(0, t1).toInt();
    e.uptime_s = line.substring(t1 + 1, t2).toInt();
    snprintf(e.msg, sizeof e.msg, "%s", line.substring(t2 + 1).c_str());
  }
  f.close();
}

void EventLog::clear() {
  head_ = 0; n_ = 0;
  if (fs_ok_) LittleFS.remove(PATH);
}
