#include "nut_protocol.h"
#include <vector>
#include <cstdio>
#include <cstring>

namespace nut {

int tokenize(const std::string& line, std::string* toks, int max) {
  int n = 0;
  size_t i = 0, len = line.size();
  while (i < len && n < max) {
    while (i < len && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) i++;
    if (i >= len) break;
    if (line[i] == '#') break;
    std::string t;
    if (line[i] == '"') {
      i++;
      while (i < len && line[i] != '"') {
        if (line[i] == '\\' && i + 1 < len) i++;
        t += line[i++];
      }
      if (i < len) i++;  // closing quote
    } else {
      while (i < len && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') {
        if (line[i] == '\\' && i + 1 < len) i++;
        t += line[i++];
      }
    }
    toks[n++] = t;
  }
  return n;
}

std::string quote(const char* s) {
  std::string o = "\"";
  for (; *s; ++s) {
    if (*s == '"' || *s == '\\') o += '\\';
    o += *s;
  }
  o += '"';
  return o;
}

static bool ieq(const std::string& a, const char* b) { return strcasecmp(a.c_str(), b) == 0; }

bool Session::authed(const Config& cfg) const {
  if (!cfg.username || !*cfg.username) return true;  // open server
  return user_set_ && pass_set_ && user_ == cfg.username && pass_ == cfg.password;
}

static std::string type_of(const ups::Var& v) {
  std::string t;
  if (v.flags & ups::F_RW) t += "RW ";
  if (v.flags & ups::F_STRING) {
    char b[24];
    snprintf(b, sizeof b, "STRING:%d", ups::VALUE_LEN - 1);
    t += b;
  } else {
    t += "NUMBER";
  }
  return t;
}

bool Session::handle_line(const std::string& line, const Config& cfg, const ups::State& st,
                          const Hooks& hooks, std::string& out, int num_logins,
                          const std::vector<std::string>* clients) {
  std::string t[8];
  int n = tokenize(line, t, 8);
  if (n == 0) return true;  // blank line: ignore
  const std::string& ups_name = t[1];
  auto ups_ok = [&](int idx) { return n > idx && t[idx] == cfg.ups_name; };
  auto err = [&](const char* code) { out += "ERR "; out += code; out += "\n"; };
  char buf[160];

  if (ieq(t[0], "HELP")) {
    out += "Commands: HELP VER NETVER PROTVER GET LIST SET INSTCMD LOGIN LOGOUT USERNAME PASSWORD STARTTLS PRIMARY MASTER FSD\n";
    return true;
  }
  if (ieq(t[0], "VER")) {
    snprintf(buf, sizeof buf, "UPS Watcher upsd %s (Network UPS Tools protocol) - https://github.com/djb-rh/ups-watcher\n", cfg.version);
    out += buf;
    return true;
  }
  if (ieq(t[0], "NETVER") || ieq(t[0], "PROTVER")) { out += "1.3\n"; return true; }
  if (ieq(t[0], "STARTTLS")) { err("FEATURE-NOT-CONFIGURED"); return true; }
  if (ieq(t[0], "USERNAME")) {
    if (n < 2) { err("INVALID-ARGUMENT"); return true; }
    if (user_set_) { err("ALREADY-SET-USERNAME"); return true; }
    user_ = t[1]; user_set_ = true; out += "OK\n"; return true;
  }
  if (ieq(t[0], "PASSWORD")) {
    if (n < 2) { err("INVALID-ARGUMENT"); return true; }
    if (pass_set_) { err("ALREADY-SET-PASSWORD"); return true; }
    pass_ = t[1]; pass_set_ = true; out += "OK\n"; return true;
  }
  if (ieq(t[0], "LOGIN")) {
    if (n < 2) { err("INVALID-ARGUMENT"); return true; }
    if (!ups_ok(1)) { err("UNKNOWN-UPS"); return true; }
    if (cfg.username && *cfg.username) {
      if (!user_set_) { err("USERNAME-REQUIRED"); return true; }
      if (!pass_set_) { err("PASSWORD-REQUIRED"); return true; }
      if (!authed(cfg)) { err("ACCESS-DENIED"); return true; }
    }
    if (logged_in_) { err("ALREADY-LOGGED-IN"); return true; }
    logged_in_ = true; out += "OK\n"; return true;
  }
  if (ieq(t[0], "LOGOUT")) { out += "OK Goodbye\n"; return false; }
  if (ieq(t[0], "PRIMARY") || ieq(t[0], "MASTER")) {
    if (!ups_ok(1)) { err("UNKNOWN-UPS"); return true; }
    if (!authed(cfg)) { err("ACCESS-DENIED"); return true; }
    out += ieq(t[0], "PRIMARY") ? "OK PRIMARY-GRANTED\n" : "OK MASTER-GRANTED\n";
    return true;
  }
  if (ieq(t[0], "FSD")) {
    // We never forward a forced shutdown to the UPS.
    if (!ups_ok(1)) { err("UNKNOWN-UPS"); return true; }
    err("ACCESS-DENIED"); return true;
  }

  if (ieq(t[0], "GET")) {
    if (n < 2) { err("INVALID-ARGUMENT"); return true; }
    if (ieq(t[1], "TRACKING")) { out += "OFF\n"; return true; }
    if (n < 3) { err("INVALID-ARGUMENT"); return true; }
    if (t[2] != cfg.ups_name) { err("UNKNOWN-UPS"); return true; }
    if (ieq(t[1], "NUMLOGINS")) {
      snprintf(buf, sizeof buf, "NUMLOGINS %s %d\n", cfg.ups_name, num_logins); out += buf; return true;
    }
    if (ieq(t[1], "UPSDESC")) {
      out += "UPSDESC "; out += cfg.ups_name; out += " "; out += quote(cfg.ups_desc); out += "\n"; return true;
    }
    if (n < 4) { err("INVALID-ARGUMENT"); return true; }
    if (ieq(t[1], "VAR")) {
      const ups::Var* v = st.find(t[3].c_str());
      if (!v) { err("VAR-NOT-SUPPORTED"); return true; }
      if (st.stale()) { err("DATA-STALE"); return true; }
      out += "VAR "; out += cfg.ups_name; out += " "; out += v->name; out += " "; out += quote(v->value); out += "\n";
      return true;
    }
    if (ieq(t[1], "TYPE")) {
      const ups::Var* v = st.find(t[3].c_str());
      if (!v) { err("VAR-NOT-SUPPORTED"); return true; }
      out += "TYPE "; out += cfg.ups_name; out += " "; out += v->name; out += " "; out += type_of(*v); out += "\n";
      return true;
    }
    if (ieq(t[1], "DESC")) {
      const ups::Var* v = st.find(t[3].c_str());
      if (!v) { err("VAR-NOT-SUPPORTED"); return true; }
      out += "DESC "; out += cfg.ups_name; out += " "; out += v->name; out += " "; out += quote(ups::describe_var(v->name)); out += "\n";
      return true;
    }
    if (ieq(t[1], "CMDDESC")) {
      if (!st.has_cmd(t[3].c_str())) { err("CMD-NOT-SUPPORTED"); return true; }
      out += "CMDDESC "; out += cfg.ups_name; out += " "; out += t[3]; out += " "; out += quote(ups::describe_cmd(t[3].c_str())); out += "\n";
      return true;
    }
    err("INVALID-ARGUMENT"); return true;
  }

  if (ieq(t[0], "LIST")) {
    if (n < 2) { err("INVALID-ARGUMENT"); return true; }
    if (ieq(t[1], "UPS")) {
      out += "BEGIN LIST UPS\nUPS "; out += cfg.ups_name; out += " "; out += quote(cfg.ups_desc); out += "\nEND LIST UPS\n";
      return true;
    }
    if (n < 3) { err("INVALID-ARGUMENT"); return true; }
    if (t[2] != cfg.ups_name) { err("UNKNOWN-UPS"); return true; }
    std::string head = "BEGIN LIST " + t[1] + " " + cfg.ups_name;
    for (int i = 3; i < n; ++i) head += " " + t[i];
    std::string tail = "END" + head.substr(5) + "\n";
    head += "\n";
    if (ieq(t[1], "VAR") || ieq(t[1], "RW")) {
      bool rw = ieq(t[1], "RW");
      if (st.stale() && !rw) { err("DATA-STALE"); return true; }
      out += head;
      for (int i = 0; ; ++i) {
        const ups::Var* v = st.at(i);
        if (!v) break;
        if (rw && !(v->flags & ups::F_RW)) continue;
        out += rw ? "RW " : "VAR "; out += cfg.ups_name; out += " "; out += v->name; out += " "; out += quote(v->value); out += "\n";
      }
      out += tail; return true;
    }
    if (ieq(t[1], "CMD")) {
      out += head;
      for (int i = 0; i < st.cmd_count(); ++i) { out += "CMD "; out += cfg.ups_name; out += " "; out += st.cmd(i); out += "\n"; }
      out += tail; return true;
    }
    if (ieq(t[1], "ENUM") || ieq(t[1], "RANGE")) {
      if (n < 4) { err("INVALID-ARGUMENT"); return true; }
      if (!st.find(t[3].c_str())) { err("VAR-NOT-SUPPORTED"); return true; }
      out += head; out += tail; return true;
    }
    if (ieq(t[1], "CLIENT")) {
      out += head;
      if (clients) for (auto& c : *clients) { out += "CLIENT "; out += cfg.ups_name; out += " "; out += c; out += "\n"; }
      out += tail; return true;
    }
    err("INVALID-ARGUMENT"); return true;
  }

  if (ieq(t[0], "INSTCMD")) {
    if (n < 3) { err("INVALID-ARGUMENT"); return true; }
    if (!ups_ok(1)) { err("UNKNOWN-UPS"); return true; }
    if (cfg.username && *cfg.username) {
      if (!user_set_) { err("USERNAME-REQUIRED"); return true; }
      if (!pass_set_) { err("PASSWORD-REQUIRED"); return true; }
      if (!authed(cfg)) { err("ACCESS-DENIED"); return true; }
    }
    if (!st.has_cmd(t[2].c_str())) { err("CMD-NOT-SUPPORTED"); return true; }
    if (!hooks.instcmd || !hooks.instcmd(t[2].c_str(), n > 3 ? t[3].c_str() : nullptr)) { err("INSTCMD-FAILED"); return true; }
    out += "OK\n"; return true;
  }

  if (ieq(t[0], "SET")) {
    if (n >= 3 && ieq(t[1], "TRACKING")) { out += "OK\n"; return true; }
    if (n < 5 || !ieq(t[1], "VAR")) { err("INVALID-ARGUMENT"); return true; }
    if (!ups_ok(2)) { err("UNKNOWN-UPS"); return true; }
    if (cfg.username && *cfg.username) {
      if (!user_set_) { err("USERNAME-REQUIRED"); return true; }
      if (!pass_set_) { err("PASSWORD-REQUIRED"); return true; }
      if (!authed(cfg)) { err("ACCESS-DENIED"); return true; }
    }
    const ups::Var* v = st.find(t[3].c_str());
    if (!v) { err("VAR-NOT-SUPPORTED"); return true; }
    if (!(v->flags & ups::F_RW)) { err("READONLY"); return true; }
    if (t[4].size() >= ups::VALUE_LEN) { err("TOO-LONG"); return true; }
    if (!hooks.setvar || !hooks.setvar(t[3].c_str(), t[4].c_str())) { err("SET-FAILED"); return true; }
    out += "OK\n"; return true;
  }

  err("UNKNOWN-COMMAND");
  return true;
}

}  // namespace nut
