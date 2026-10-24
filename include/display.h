#pragma once
#include <Arduino.h>

class Display {
 public:
  void begin();
  void task();
  void wake();
  void next_page();
  void prev_page();
  void toggle_light();
  int page() const { return page_; }

 private:
  void draw_overview(bool full);
  void draw_detail(bool full);
  void draw_events(bool full);
  void draw_network(bool full);
  void draw_header(bool full);
  void draw_footer(const char* a, const char* b, const char* c);
  int page_ = 0;
  bool lit_ = true;
  uint32_t last_activity_ms_ = 0, last_draw_ms_ = 0;
  uint32_t last_gen_ = 0xFFFFFFFF;
  int last_page_ = -1;
  bool last_alarm_ = false;
  String last_header_;
};

extern Display display;
