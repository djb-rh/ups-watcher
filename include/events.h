// Event log: a RAM ring buffer mirrored to LittleFS so power-loss history
// survives a reboot.
#pragma once
#include <Arduino.h>
#include <time.h>

struct Event {
  time_t ts;         // 0 if the clock was not set yet
  uint32_t uptime_s;
  char msg[80];
};

class EventLog {
 public:
  static constexpr int CAP = 48;
  void begin();
  void add(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
  int count() const { return n_; }
  // 0 = newest
  const Event& newest(int i) const { return ring_[(head_ - 1 - i + CAP * 2) % CAP]; }
  static String format_time(const Event& e);
  void clear();

 private:
  Event ring_[CAP];
  int head_ = 0, n_ = 0;
  bool fs_ok_ = false;
  void persist(const Event& e);
  void load();
};

extern EventLog events;
