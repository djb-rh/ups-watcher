// The upsd side of the NUT network protocol (version 1.3), independent of
// any socket so it can be unit-tested on the host. One Session per TCP
// client; feed it complete lines (without the trailing LF) and it appends
// the reply to `out`.
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <functional>
#include "ups_state.h"

namespace nut {

struct Config {
  const char* ups_name = "ups";
  const char* ups_desc = "UPS Watcher";
  const char* username = "";   // empty = no authentication required
  const char* password = "";
  const char* version = "0.0";
};

// Callback for INSTCMD / SET VAR: returns true on success.
using InstCmdFn = std::function<bool(const char* cmd, const char* param)>;
using SetVarFn = std::function<bool(const char* var, const char* value)>;

struct Hooks {
  InstCmdFn instcmd;
  SetVarFn setvar;
  // Address string for LIST CLIENT, per session.
};

class Session {
 public:
  Session() = default;
  void reset(const char* peer_addr) {
    user_.clear(); pass_.clear(); logged_in_ = false; user_set_ = false; pass_set_ = false;
    peer_ = peer_addr ? peer_addr : "";
  }
  bool logged_in() const { return logged_in_; }
  const std::string& peer() const { return peer_; }
  // Returns false when the client asked to disconnect (LOGOUT).
  bool handle_line(const std::string& line, const Config& cfg, const ups::State& st,
                   const Hooks& hooks, std::string& out, int num_logins,
                   const std::vector<std::string>* clients);

 private:
  std::string user_, pass_, peer_;
  bool logged_in_ = false, user_set_ = false, pass_set_ = false;
  bool authed(const Config& cfg) const;
};

// Splits a protocol line into tokens, honouring "quotes" and \ escapes.
int tokenize(const std::string& line, std::string* toks, int max);
// Quotes a value the way upsd does (escape \ and ").
std::string quote(const char* s);

}  // namespace nut
