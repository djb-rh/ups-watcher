#include "nut_server.h"

NutServer nutServer;

void NutServer::begin(uint16_t port) {
  if (server_) return;
  server_ = new WiFiServer(port);
  server_->begin();
  server_->setNoDelay(true);
  Serial.printf("[nut] listening on %u\r\n", port);
}

int NutServer::client_count() const {
  int n = 0;
  for (auto& s : slots_) if (s.used) n++;
  return n;
}

int NutServer::login_count() const {
  int n = 0;
  for (auto& s : slots_) if (s.used && s.s.logged_in()) n++;
  return n;
}

std::vector<std::string> NutServer::client_addrs() const {
  std::vector<std::string> v;
  for (auto& s : slots_) if (s.used) v.push_back(s.s.peer());
  return v;
}

void NutServer::task(const ups::State& st, const nut::Config& cfg, const nut::Hooks& hooks) {
  if (!server_) return;
  // Accept
  WiFiClient nc = server_->accept();
  if (nc) {
    Slot* free_slot = nullptr;
    for (auto& s : slots_) if (!s.used) { free_slot = &s; break; }
    if (!free_slot) {
      nc.print("ERR ACCESS-DENIED\n");
      nc.stop();
    } else {
      free_slot->c = nc;
      free_slot->used = true;
      free_slot->buf = "";
      free_slot->last_ms = millis();
      free_slot->s.reset(nc.remoteIP().toString().c_str());
      free_slot->c.setNoDelay(true);
    }
  }
  std::vector<std::string> addrs;
  for (auto& s : slots_) {
    if (!s.used) continue;
    if (!s.c.connected()) { s.c.stop(); s.used = false; continue; }
    // idle for 10 minutes without a logged-in session: drop
    if (!s.s.logged_in() && millis() - s.last_ms > 10 * 60 * 1000) { s.c.stop(); s.used = false; continue; }
    int avail = s.c.available();
    if (avail <= 0) continue;
    s.last_ms = millis();
    while (avail-- > 0) {
      int ch = s.c.read();
      if (ch < 0) break;
      if (ch == '\n') {
        std::string out;
        if (addrs.empty()) addrs = client_addrs();
        bool keep = s.s.handle_line(std::string(s.buf.c_str()), cfg, st, hooks, out, login_count(), &addrs);
        commands_++;
        if (!out.empty()) s.c.write((const uint8_t*)out.data(), out.size());
        s.buf = "";
        if (!keep) { s.c.flush(); delay(5); s.c.stop(); s.used = false; break; }
      } else if (ch != '\r') {
        if (s.buf.length() < 512) s.buf += (char)ch;
      }
    }
  }
}
