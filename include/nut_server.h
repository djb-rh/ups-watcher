// TCP front end for nut::Session on port 3493.
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <vector>
#include "nut_protocol.h"

class NutServer {
 public:
  static constexpr int MAX_CLIENTS = 6;
  void begin(uint16_t port = 3493);
  void task(const ups::State& st, const nut::Config& cfg, const nut::Hooks& hooks);
  int client_count() const;
  int login_count() const;
  std::vector<std::string> client_addrs() const;
  uint32_t commands_served() const { return commands_; }

 private:
  struct Slot {
    WiFiClient c;
    nut::Session s;
    String buf;
    bool used = false;
    uint32_t last_ms = 0;
  };
  WiFiServer* server_ = nullptr;
  Slot slots_[MAX_CLIENTS];
  uint32_t commands_ = 0;
};

extern NutServer nutServer;
