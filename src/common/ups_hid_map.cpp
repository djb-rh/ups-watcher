#include "ups_hid_map.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace upsmap {

// Order matters: the first path found in the device's descriptor wins.
const VarEntry kVarMap[] = {
  {"UPS.PowerSummary.iManufacturer", "ups.mfr", C_STRINGID, 0, M_SEMI},
  {"UPS.iManufacturer", "ups.mfr", C_STRINGID, 0, M_SEMI},
  {"UPS.PowerSummary.iProduct", "ups.model", C_STRINGID, 0, M_SEMI},
  {"UPS.iProduct", "ups.model", C_STRINGID, 0, M_SEMI},
  {"UPS.PowerSummary.iSerialNumber", "ups.serial", C_STRINGID, 0, M_SEMI},
  {"UPS.iSerialNumber", "ups.serial", C_STRINGID, 0, M_SEMI},
  {"UPS.PowerSummary.iDeviceChemistry", "battery.type", C_STRINGID, 0, M_SEMI},
  {"UPS.PowerSummary.RemainingCapacity", "battery.charge", C_NUM, 0, 0},
  {"UPS.Battery.RemainingCapacity", "battery.charge", C_NUM, 0, 0},
  {"UPS.PowerSummary.RemainingCapacityLimit", "battery.charge.low", C_NUM, 0, M_RW | M_SEMI},
  {"UPS.PowerSummary.WarningCapacityLimit", "battery.charge.warning", C_NUM, 0, M_SEMI},
  {"UPS.PowerSummary.RunTimeToEmpty", "battery.runtime", C_NUM, 0, 0},
  {"UPS.Battery.RunTimeToEmpty", "battery.runtime", C_NUM, 0, 0},
  {"UPS.PowerSummary.RemainingTimeLimit", "battery.runtime.low", C_NUM, 0, M_RW | M_SEMI},
  {"UPS.Battery.RemainingTimeLimit", "battery.runtime.low", C_NUM, 0, M_RW | M_SEMI},
  {"UPS.PowerSummary.Voltage", "battery.voltage", C_NUM, 1, 0},
  {"UPS.Battery.Voltage", "battery.voltage", C_NUM, 1, 0},
  {"UPS.PowerSummary.ConfigVoltage", "battery.voltage.nominal", C_NUM, 1, M_SEMI},
  {"UPS.Battery.ConfigVoltage", "battery.voltage.nominal", C_NUM, 1, M_SEMI},
  {"UPS.Battery.Temperature", "battery.temperature", C_KELVIN, 1, 0},
  {"UPS.Battery.ManufacturerDate", "battery.mfr.date", C_DATE, 0, M_SEMI},
  {"UPS.PowerSummary.ManufacturerDate", "battery.mfr.date", C_DATE, 0, M_SEMI},
  {"UPS.Battery.APCBattReplaceDate", "battery.date", C_APCDATE, 0, M_SEMI},
  {"UPS.ManufacturerDate", "ups.mfr.date", C_DATE, 0, M_SEMI},
  {"UPS.PowerConverter.PercentLoad", "ups.load", C_NUM, 0, 0},
  {"UPS.Output.PercentLoad", "ups.load", C_NUM, 1, 0},
  {"UPS.PowerConverter.ConfigActivePower", "ups.realpower.nominal", C_NUM, 0, M_SEMI},
  {"UPS.Output.ConfigActivePower", "ups.realpower.nominal", C_NUM, 0, M_SEMI},
  {"UPS.Output.ConfigApparentPower", "ups.power.nominal", C_NUM, 0, M_SEMI},
  {"UPS.PowerSummary.AudibleAlarmControl", "ups.beeper.status", C_BEEPER, 0, 0},
  {"UPS.AudibleAlarmControl", "ups.beeper.status", C_BEEPER, 0, 0},
  {"UPS.Battery.Test", "ups.test.result", C_TESTRES, 0, 0},
  {"UPS.BatterySystem.Battery.Test", "ups.test.result", C_TESTRES, 0, 0},
  {"UPS.PowerSummary.DelayBeforeShutdown", "ups.timer.shutdown", C_NUM, 0, 0},
  {"UPS.PowerSummary.DelayBeforeStartup", "ups.timer.start", C_NUM, 0, 0},
  {"UPS.PowerSummary.DelayBeforeReboot", "ups.timer.reboot", C_NUM, 0, 0},
  {"UPS.APCGeneralCollection.APCDelayBeforeReboot", "ups.timer.reboot", C_NUM, 0, 0},
  {"UPS.Input.Voltage", "input.voltage", C_NUM, 1, 0},
  {"UPS.Input.ConfigVoltage", "input.voltage.nominal", C_NUM, 0, M_SEMI},
  {"UPS.Input.LowVoltageTransfer", "input.transfer.low", C_NUM, 0, M_RW | M_SEMI},
  {"UPS.Output.LowVoltageTransfer", "input.transfer.low", C_NUM, 0, M_RW | M_SEMI},
  {"UPS.Input.HighVoltageTransfer", "input.transfer.high", C_NUM, 0, M_RW | M_SEMI},
  {"UPS.Output.HighVoltageTransfer", "input.transfer.high", C_NUM, 0, M_RW | M_SEMI},
  {"UPS.Input.APCSensitivity", "input.sensitivity", C_SENS, 0, M_RW | M_SEMI},
  {"UPS.Input.APCLineFailCause", "input.transfer.reason", C_LINEFAIL, 0, 0},
  {"UPS.Input.Frequency", "input.frequency", C_NUM, 1, 0},
  {"UPS.Output.Voltage", "output.voltage", C_NUM, 1, 0},
  {"UPS.Output.ConfigVoltage", "output.voltage.nominal", C_NUM, 1, M_SEMI},
  {"UPS.Output.Current", "output.current", C_NUM, 2, 0},
  {"UPS.Output.Frequency", "output.frequency", C_NUM, 1, 0},
  {"UPS.APCEnvironment.APCProbe1.Temperature", "ambient.temperature", C_KELVIN, 1, 0},
  {"UPS.APCEnvironment.APCProbe1.Humidity", "ambient.humidity", C_NUM, 1, 0},
};
const int kVarMapCount = sizeof kVarMap / sizeof kVarMap[0];

const StatusEntry kStatusMap[] = {
  {"UPS.PowerSummary.PresentStatus.ACPresent", S_ONLINE, false},
  {"UPS.PowerSummary.ACPresent", S_ONLINE, false},
  {"UPS.PowerSummary.PresentStatus.Discharging", S_DISCHRG, false},
  {"UPS.PowerSummary.Discharging", S_DISCHRG, false},
  {"UPS.PowerSummary.PresentStatus.Charging", S_CHRG, false},
  {"UPS.PowerSummary.Charging", S_CHRG, false},
  {"UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit", S_LOWBATT, false},
  {"UPS.PowerSummary.BelowRemainingCapacityLimit", S_LOWBATT, false},
  {"UPS.PowerSummary.PresentStatus.ShutdownImminent", S_SHUTDOWNIMM, false},
  {"UPS.PowerSummary.ShutdownImminent", S_SHUTDOWNIMM, false},
  {"UPS.PowerSummary.PresentStatus.RemainingTimeLimitExpired", S_TIMELIMITEXP, false},
  {"UPS.PowerSummary.PresentStatus.Overload", S_OVERLOAD, false},
  {"UPS.PowerSummary.PresentStatus.NeedReplacement", S_REPLACEBATT, false},
  {"UPS.PowerSummary.PresentStatus.BatteryPresent", S_NOBATTERY, true},
  {"UPS.PowerSummary.PresentStatus.FullyCharged", S_FULLYCHARGED, false},
  {"UPS.PowerSummary.PresentStatus.CommunicationLost", S_COMMFAULT, false},
  {"UPS.PowerSummary.PresentStatus.InternalFailure", S_COMMFAULT, false},
  {"UPS.PowerSummary.PresentStatus.OverTemperature", S_OVERHEAT, false},
  {"UPS.PowerSummary.PresentStatus.Boost", S_BOOST, false},
  {"UPS.PowerSummary.PresentStatus.Buck", S_TRIM, false},
  {"UPS.PowerSummary.PresentStatus.AwaitingPower", S_AWAITINGPOWER, false},
  {"UPS.PowerSummary.PresentStatus.VoltageOutOfRange", S_VRANGE, false},
  {"UPS.PowerSummary.PresentStatus.FrequencyOutOfRange", S_FRANGE, false},
};
const int kStatusMapCount = sizeof kStatusMap / sizeof kStatusMap[0];

const CmdEntry kCmdMap[] = {
  {"beeper.enable", "UPS.PowerSummary.AudibleAlarmControl", 2},
  {"beeper.disable", "UPS.PowerSummary.AudibleAlarmControl", 1},
  {"beeper.mute", "UPS.PowerSummary.AudibleAlarmControl", 3},
  {"beeper.on", "UPS.PowerSummary.AudibleAlarmControl", 2},
  {"beeper.off", "UPS.PowerSummary.AudibleAlarmControl", 3},
  {"test.battery.start.quick", "UPS.Battery.Test", 1},
  {"test.battery.start.quick", "UPS.BatterySystem.Battery.Test", 1},
  {"test.battery.start.deep", "UPS.Battery.Test", 2},
  {"test.battery.start.deep", "UPS.BatterySystem.Battery.Test", 2},
  {"test.battery.stop", "UPS.Battery.Test", 3},
  {"test.battery.stop", "UPS.BatterySystem.Battery.Test", 3},
};
const int kCmdMapCount = sizeof kCmdMap / sizeof kCmdMap[0];

const SetEntry kSetMap[] = {
  {"battery.charge.low", "UPS.PowerSummary.RemainingCapacityLimit", C_NUM},
  {"battery.runtime.low", "UPS.PowerSummary.RemainingTimeLimit", C_NUM},
  {"battery.runtime.low", "UPS.Battery.RemainingTimeLimit", C_NUM},
  {"input.transfer.low", "UPS.Input.LowVoltageTransfer", C_NUM},
  {"input.transfer.low", "UPS.Output.LowVoltageTransfer", C_NUM},
  {"input.transfer.high", "UPS.Input.HighVoltageTransfer", C_NUM},
  {"input.transfer.high", "UPS.Output.HighVoltageTransfer", C_NUM},
  {"input.sensitivity", "UPS.Input.APCSensitivity", C_SENS},
};
const int kSetMapCount = sizeof kSetMap / sizeof kSetMap[0];

static void append(char* out, size_t n, const char* tok) {
  size_t l = strlen(out);
  if (l && l + 1 < n) { out[l++] = ' '; out[l] = 0; }
  strncat(out, tok, n - strlen(out) - 1);
}

void build_status(uint32_t f, int charge, char* out, size_t n) {
  out[0] = 0;
  if (f & S_ONLINE) append(out, n, "OL"); else append(out, n, "OB");
  if (f & S_DISCHRG) append(out, n, "DISCHRG");
  if (f & S_HAVE_CHG_FLAGS) {
    if ((f & S_CHRG) && !(f & S_FULLYCHARGED)) append(out, n, "CHRG");
  } else if ((f & S_ONLINE) && charge >= 1 && charge <= 99) {
    append(out, n, "CHRG");
  }
  if (f & (S_LOWBATT | S_TIMELIMITEXP | S_SHUTDOWNIMM)) append(out, n, "LB");
  if (f & (S_REPLACEBATT | S_NOBATTERY)) append(out, n, "RB");
  if (f & S_OVERLOAD) append(out, n, "OVER");
  if (f & S_TRIM) append(out, n, "TRIM");
  if (f & S_BOOST) append(out, n, "BOOST");
  if (f & S_BYPASS) append(out, n, "BYPASS");
  if (f & S_OFF) append(out, n, "OFF");
}

void build_alarm(uint32_t f, char* out, size_t n) {
  out[0] = 0;
  if (f & S_REPLACEBATT) append(out, n, "Replace battery!");
  if (f & S_SHUTDOWNIMM) append(out, n, "Shutdown imminent!");
  if (f & S_NOBATTERY) append(out, n, "No battery installed!");
  if (f & S_OVERHEAT) append(out, n, "Temperature too high!");
  if (f & S_COMMFAULT) append(out, n, "Internal UPS fault!");
  if (f & S_AWAITINGPOWER) append(out, n, "Awaiting power!");
}

const char* beeper_str(long v) {
  switch (v) { case 1: return "disabled"; case 2: return "enabled"; case 3: return "muted"; default: return "unknown"; }
}
const char* test_result_str(long v) {
  switch (v) {
    case 1: return "Done and passed"; case 2: return "Done and warning"; case 3: return "Done and error";
    case 4: return "Aborted"; case 5: return "In progress"; case 6: return "No test initiated";
    default: return "Unknown";
  }
}
const char* sensitivity_str(long v) {
  switch (v) { case 0: return "low"; case 1: return "medium"; case 2: return "high"; default: return "unknown"; }
}
long sensitivity_value(const char* s) {
  if (!strcmp(s, "low") || !strcmp(s, "0")) return 0;
  if (!strcmp(s, "medium") || !strcmp(s, "1")) return 1;
  if (!strcmp(s, "high") || !strcmp(s, "2")) return 2;
  return -1;
}
const char* linefail_str(long v) {
  static const char* k[] = {
    "No transfers since turnon", "High line voltage", "Brownout", "Loss of mains power",
    "Small temporary sag", "Large temporary sag", "Small spike", "Large spike", "UPS self test",
    "Excessive input voltage fluctuation", "Unknown 10", "Unknown 11", "Unknown 12", "Unknown 13"};
  if (v >= 0 && v < (long)(sizeof k / sizeof k[0])) return k[v];
  return "Unknown";
}
void hid_date_str(long v, char* out, size_t n) {
  if (v <= 0) { snprintf(out, n, "not set"); return; }
  snprintf(out, n, "%04ld/%02ld/%02ld", 1980 + ((v >> 9) & 0x7F), (v >> 5) & 0x0F, v & 0x1F);
}
void apc_date_str(long v, char* out, size_t n) {
  // APC packs the date as BCD digits: 0xMMDDYY (raw bytes YY DD MM), the same
  // way NUT's apc_date_conversion_fun reads it. 0 = not set.
  if (v <= 0) { snprintf(out, n, "not set"); return; }
  int year = (v & 0xF) + 10 * ((v >> 4) & 0xF);
  int day = ((v >> 8) & 0xF) + 10 * ((v >> 12) & 0xF);
  int month = ((v >> 16) & 0xF) + 10 * ((v >> 20) & 0xF);
  year += year >= 70 ? 1900 : 2000;
  snprintf(out, n, "%04d/%02d/%02d", year, month, day);
}

static void rtrim(char* s) {
  size_t l = strlen(s);
  while (l && (s[l - 1] == ' ' || s[l - 1] == '\t')) s[--l] = 0;
}

void split_apc_product(const char* product, char* model, size_t ml, char* fw, size_t fl, char* fwaux, size_t al) {
  snprintf(model, ml, "%s", product);
  fw[0] = 0; fwaux[0] = 0;
  char* p = strstr(model, " FW:");
  if (p) {
    snprintf(fw, fl, "%s", p + 4);
    *p = 0;
    char* u = strstr(fw, " USB FW:");
    if (u) { snprintf(fwaux, al, "%s", u + 8); *u = 0; }
  }
  rtrim(model); rtrim(fw); rtrim(fwaux);
}

}  // namespace upsmap
