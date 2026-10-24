#pragma once
#include <Arduino.h>

class WebUi {
 public:
  void begin();
  void task();
  void set_captive(bool on);
  bool captive() const { return captive_; }

 private:
  bool captive_ = false;
};

extern WebUi webUi;
