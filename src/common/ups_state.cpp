#include "ups_state.h"
#include <cstdio>

namespace ups {

State::State() { clear(); }

void State::clear() {
  memset(vars_, 0, sizeof vars_);
  n_cmds_ = 0;
  stale_ = true;
  gen_++;
}

const Var* State::find(const char* name) const {
  for (int i = 0; i < MAX_VARS; ++i)
    if (vars_[i].used && strcmp(vars_[i].name, name) == 0) return &vars_[i];
  return nullptr;
}

bool State::set(const char* name, const char* value, uint8_t flags) {
  Var* slot = nullptr;
  for (int i = 0; i < MAX_VARS; ++i) {
    if (vars_[i].used && strcmp(vars_[i].name, name) == 0) { slot = &vars_[i]; break; }
    if (!slot && !vars_[i].used) slot = &vars_[i];
  }
  // Prefer an existing entry over the first free one.
  for (int i = 0; i < MAX_VARS; ++i)
    if (vars_[i].used && strcmp(vars_[i].name, name) == 0) { slot = &vars_[i]; break; }
  if (!slot) return false;
  if (!slot->used) {
    slot->used = true;
    snprintf(slot->name, sizeof slot->name, "%s", name);
  }
  if (strncmp(slot->value, value, sizeof slot->value) != 0 || slot->flags != flags) gen_++;
  snprintf(slot->value, sizeof slot->value, "%s", value);
  slot->flags = flags;
  return true;
}

bool State::set_num(const char* name, double value, int decimals, uint8_t flags) {
  char buf[32];
  snprintf(buf, sizeof buf, "%.*f", decimals, value);
  return set(name, buf, flags);
}

bool State::set_int(const char* name, long value, uint8_t flags) {
  char buf[24];
  snprintf(buf, sizeof buf, "%ld", value);
  return set(name, buf, flags);
}

void State::remove(const char* name) {
  for (int i = 0; i < MAX_VARS; ++i)
    if (vars_[i].used && strcmp(vars_[i].name, name) == 0) { vars_[i].used = false; gen_++; }
}

const char* State::get(const char* name) const {
  const Var* v = find(name);
  return v ? v->value : nullptr;
}

int State::size() const {
  int n = 0;
  for (int i = 0; i < MAX_VARS; ++i) if (vars_[i].used) n++;
  return n;
}

const Var* State::at(int index) const {
  int n = 0;
  for (int i = 0; i < MAX_VARS; ++i) {
    if (!vars_[i].used) continue;
    if (n == index) return &vars_[i];
    n++;
  }
  return nullptr;
}

bool State::add_cmd(const char* name) {
  if (has_cmd(name)) return true;
  if (n_cmds_ >= 16) return false;
  snprintf(cmds_[n_cmds_++], 32, "%s", name);
  gen_++;
  return true;
}

void State::clear_cmds() { n_cmds_ = 0; gen_++; }

bool State::has_cmd(const char* name) const {
  for (int i = 0; i < n_cmds_; ++i) if (strcmp(cmds_[i], name) == 0) return true;
  return false;
}

struct Desc { const char* name; const char* desc; };
static const Desc kVarDesc[] = {
  {"device.mfr", "Device manufacturer"}, {"device.model", "Device model"},
  {"device.serial", "Device serial number"}, {"device.type", "Device type (ups, pdu, scd, psu, ats)"},
  {"driver.name", "Driver name"}, {"driver.version", "Driver version - NUT release"},
  {"driver.version.internal", "Internal driver version"}, {"driver.parameter.pollinterval", "Driver poll interval"},
  {"driver.parameter.port", "Device port"}, {"driver.state", "Current state in driver lifecycle"},
  {"ups.mfr", "UPS manufacturer"}, {"ups.model", "UPS model"}, {"ups.serial", "UPS serial number"},
  {"ups.firmware", "UPS firmware"}, {"ups.firmware.aux", "Auxiliary device firmware"},
  {"ups.status", "UPS status"}, {"ups.alarm", "UPS alarms"}, {"ups.load", "Load on UPS (percent of full)"},
  {"ups.beeper.status", "UPS beeper status"}, {"ups.test.result", "Results of last self test"},
  {"ups.delay.shutdown", "Interval to wait after shutdown with delay command (seconds)"},
  {"ups.delay.start", "Interval to wait before (re)starting the load (seconds)"},
  {"ups.timer.shutdown", "Time before the load will be shutdown (seconds)"},
  {"ups.timer.start", "Time before the load will be started (seconds)"},
  {"ups.timer.reboot", "Time before the load will be rebooted (seconds)"},
  {"ups.realpower.nominal", "UPS real power rating (W)"}, {"ups.power.nominal", "UPS power rating (VA)"},
  {"ups.mfr.date", "UPS manufacturing date"}, {"ups.vendorid", "Vendor ID for USB devices"},
  {"ups.productid", "Product ID for USB devices"},
  {"battery.charge", "Battery charge (percent of full)"}, {"battery.charge.low", "Remaining battery level when UPS switches to LB (percent)"},
  {"battery.charge.warning", "Battery level when UPS switches to Warning state (percent)"},
  {"battery.runtime", "Battery runtime (seconds)"}, {"battery.runtime.low", "Remaining battery runtime when UPS switches to LB (seconds)"},
  {"battery.voltage", "Battery voltage (V)"}, {"battery.voltage.nominal", "Nominal battery voltage (V)"},
  {"battery.type", "Battery chemistry"}, {"battery.mfr.date", "Battery manufacturing date"},
  {"battery.date", "Battery change date"}, {"battery.temperature", "Battery temperature (degrees C)"},
  {"input.voltage", "Input voltage (V)"}, {"input.voltage.nominal", "Nominal input voltage (V)"},
  {"input.transfer.low", "Low voltage transfer point (V)"}, {"input.transfer.high", "High voltage transfer point (V)"},
  {"input.sensitivity", "Input power sensitivity"}, {"input.frequency", "Input line frequency (Hz)"},
  {"output.voltage", "Output voltage (V)"}, {"output.voltage.nominal", "Nominal output voltage (V)"},
  {"output.frequency", "Output frequency (Hz)"}, {"output.current", "Output current (A)"},
  {"watcher.power", "UPS Watcher power source (external or battery)"},
  {"watcher.battery.charge", "UPS Watcher internal battery (percent)"},
  {"watcher.battery.status", "UPS Watcher internal battery: charging, full, discharging"},
  {"watcher.uptime", "UPS Watcher uptime (seconds)"},
  {"watcher.ups.lastseen", "Seconds since the UPS last answered"},
};
static const Desc kCmdDesc[] = {
  {"beeper.enable", "Enable the UPS beeper"}, {"beeper.disable", "Disable the UPS beeper"},
  {"beeper.mute", "Temporarily mute the UPS beeper"}, {"beeper.on", "Obsolete (use beeper.enable)"},
  {"beeper.off", "Obsolete (use beeper.disable)"},
  {"test.battery.start.quick", "Start a quick battery test"}, {"test.battery.start.deep", "Start a deep battery test"},
  {"test.battery.stop", "Stop the battery test"},
};

const char* describe_var(const char* name) {
  for (auto& d : kVarDesc) if (strcmp(d.name, name) == 0) return d.desc;
  return "Description unavailable";
}
const char* describe_cmd(const char* name) {
  for (auto& d : kCmdDesc) if (strcmp(d.name, name) == 0) return d.desc;
  return "Description unavailable";
}

}  // namespace ups
