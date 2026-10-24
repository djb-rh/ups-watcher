#include "hid_parser.h"
#include <cstring>
#include <cstdio>
#include <cmath>

namespace hid {

namespace {

struct Globals {
  uint16_t usage_page = 0;
  int32_t log_min = 0, log_max = 0, phys_min = 0, phys_max = 0;
  uint32_t unit = 0;
  int8_t unit_exp = 0;
  uint8_t report_size = 0;
  uint8_t report_id = 0;
  uint8_t report_count = 0;
};

int32_t signed_item(const uint8_t* p, int n) {
  switch (n) {
    case 1: return (int8_t)p[0];
    case 2: return (int16_t)(p[0] | (p[1] << 8));
    case 4: return (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24));
    default: return 0;
  }
}
uint32_t unsigned_item(const uint8_t* p, int n) {
  uint32_t v = 0;
  for (int i = n - 1; i >= 0; --i) v = (v << 8) | p[i];
  return v;
}

// HID 1.11 says Unit Exponent is a signed nibble; several descriptors in the
// wild encode -1 as 0x0F, others as 0xFF. Accept both.
int8_t unit_exponent(const uint8_t* p, int n) {
  if (n == 1) {
    uint8_t v = p[0];
    if (v >= 0x08 && v <= 0x0F) return (int8_t)v - 16;
    return (int8_t)v;
  }
  return (int8_t)signed_item(p, n);
}

}  // namespace

uint16_t Descriptor::take_bits(uint8_t id, uint8_t type, uint16_t nbits) {
  for (int i = 0; i < size_count_; ++i) {
    if (sizes_[i].id == id && sizes_[i].type == type) {
      uint16_t off = sizes_[i].bits;
      sizes_[i].bits += nbits;
      return off;
    }
  }
  if (size_count_ < (int)(sizeof(sizes_) / sizeof(sizes_[0]))) {
    sizes_[size_count_++] = {id, type, nbits};
  }
  return 0;
}

bool Descriptor::parse(const uint8_t* d, size_t len) {
  count_ = 0;
  size_count_ = 0;
  Globals g;
  Globals stack[4];
  int sp = 0;
  uint32_t path[MAX_PATH];
  int path_len = 0;
  uint32_t usages[32];
  int n_usages = 0;
  uint32_t usage_min = 0, usage_max = 0;
  bool have_range = false;
  uint32_t last_usage = 0;

  size_t i = 0;
  while (i < len) {
    uint8_t b = d[i++];
    if (b == 0xFE) {  // long item: skip
      if (i + 1 >= len) return false;
      uint8_t dlen = d[i];
      i += 2 + dlen;
      continue;
    }
    int n = b & 3;
    if (n == 3) n = 4;
    uint8_t tag = b >> 4, type = (b >> 2) & 3;
    if (i + n > len) return false;
    const uint8_t* p = d + i;
    uint32_t uv = unsigned_item(p, n);
    int32_t sv = signed_item(p, n);
    i += n;

    switch (type) {
      case 0:  // Main
        switch (tag) {
          case 0x8: case 0x9: case 0xB: {  // Input, Output, Feature
            uint8_t rtype = tag == 0x8 ? R_INPUT : tag == 0x9 ? R_OUTPUT : R_FEATURE;
            bool is_const = uv & 1;
            bool is_var = uv & 2;
            uint8_t cnt = g.report_count;
            int32_t lmin = g.log_min, lmax = g.log_max;
            // Common descriptor bug: Logical Maximum 0xFF meaning 255 while
            // Logical Minimum is 0. Treat it as unsigned.
            if (lmin >= 0 && lmax < lmin) {
              if (g.report_size <= 8) lmax &= 0xFF;
              else if (g.report_size <= 16) lmax &= 0xFFFF;
            }
            if (is_const || (n_usages == 0 && !have_range)) {
              take_bits(g.report_id, rtype, (uint16_t)g.report_size * cnt);
            } else if (!is_var) {
              // Array item: selector slots. Record each slot with the first
              // usage; array semantics are rarely needed for power devices.
              for (int k = 0; k < cnt; ++k) {
                uint16_t off = take_bits(g.report_id, rtype, g.report_size);
                if (count_ >= MAX_FIELDS) continue;
                Field& f = fields_[count_++];
                memset(&f, 0, sizeof f);
                f.report_id = g.report_id; f.type = rtype;
                f.bit_offset = off; f.bit_size = g.report_size;
                f.path_len = (uint8_t)(path_len > MAX_PATH ? MAX_PATH : path_len);
                memcpy(f.path, path, f.path_len * sizeof(uint32_t));
                f.usage = have_range ? usage_min : usages[0];
                f.log_min = lmin; f.log_max = lmax; f.phys_min = g.phys_min; f.phys_max = g.phys_max;
                f.unit = g.unit; f.unit_exp = g.unit_exp; f.is_variable = false; f.flags = (uint8_t)uv;
              }
            } else {
              for (int k = 0; k < cnt; ++k) {
                uint32_t u;
                if (have_range) {
                  u = usage_min + k;
                  if (u > usage_max) u = usage_max;
                } else {
                  u = usages[k < n_usages ? k : n_usages - 1];
                }
                uint16_t off = take_bits(g.report_id, rtype, g.report_size);
                if (count_ >= MAX_FIELDS) continue;
                Field& f = fields_[count_++];
                memset(&f, 0, sizeof f);
                f.report_id = g.report_id; f.type = rtype;
                f.bit_offset = off; f.bit_size = g.report_size;
                f.path_len = (uint8_t)(path_len > MAX_PATH ? MAX_PATH : path_len);
                memcpy(f.path, path, f.path_len * sizeof(uint32_t));
                f.usage = u;
                f.log_min = lmin; f.log_max = lmax; f.phys_min = g.phys_min; f.phys_max = g.phys_max;
                f.unit = g.unit; f.unit_exp = g.unit_exp; f.is_variable = true; f.is_constant = is_const;
                f.flags = (uint8_t)uv;
              }
            }
            n_usages = 0; have_range = false;
            break;
          }
          case 0xA:  // Collection
            if (path_len < MAX_PATH) path[path_len] = n_usages ? usages[0] : last_usage;
            path_len++;
            n_usages = 0; have_range = false;
            break;
          case 0xC:  // End Collection
            if (path_len > 0) path_len--;
            n_usages = 0; have_range = false;
            break;
          default: break;
        }
        break;
      case 1:  // Global
        switch (tag) {
          case 0x0: g.usage_page = (uint16_t)uv; break;
          case 0x1: g.log_min = sv; break;
          case 0x2: g.log_max = sv; break;
          case 0x3: g.phys_min = sv; break;
          case 0x4: g.phys_max = sv; break;
          case 0x5: g.unit_exp = unit_exponent(p, n); break;
          case 0x6: g.unit = uv; break;
          case 0x7: g.report_size = (uint8_t)uv; break;
          case 0x8: g.report_id = (uint8_t)uv; break;
          case 0x9: g.report_count = (uint8_t)uv; break;
          case 0xA: if (sp < 4) stack[sp++] = g; break;
          case 0xB: if (sp > 0) g = stack[--sp]; break;
          default: break;
        }
        break;
      case 2: {  // Local
        uint32_t full = (n == 4) ? uv : ((uint32_t)g.usage_page << 16) | (uv & 0xFFFF);
        switch (tag) {
          case 0x0:
            if (n_usages < 32) usages[n_usages++] = full;
            last_usage = full;
            break;
          case 0x1: usage_min = full; have_range = true; break;
          case 0x2: usage_max = full; have_range = true; last_usage = usage_min; break;
          default: break;
        }
        break;
      }
      default: break;
    }
  }
  return true;
}

const Field* Descriptor::find(uint32_t usage, const uint32_t* path, int path_len, int type) const {
  for (int i = 0; i < count_; ++i) {
    const Field& f = fields_[i];
    if (f.usage != usage) continue;
    if (type >= 0 && f.type != type) continue;
    if (path_len > f.path_len) continue;
    bool ok = true;
    for (int k = 0; k < path_len; ++k) {
      if (f.path[f.path_len - path_len + k] != path[k]) { ok = false; break; }
    }
    if (ok) return &f;
  }
  return nullptr;
}

int Descriptor::reports(uint8_t type, ReportInfo* out, int max) const {
  int n = 0;
  for (int i = 0; i < size_count_ && n < max; ++i) {
    if (sizes_[i].type == type) out[n++] = {sizes_[i].id, sizes_[i].type, sizes_[i].bits};
  }
  return n;
}

uint16_t Descriptor::report_bits(uint8_t report_id, uint8_t type) const {
  for (int i = 0; i < size_count_; ++i)
    if (sizes_[i].id == report_id && sizes_[i].type == type) return sizes_[i].bits;
  return 0;
}

int32_t Descriptor::raw_value(const Field& f, const uint8_t* report, size_t report_len) {
  uint32_t v = 0;
  for (int b = 0; b < f.bit_size && b < 32; ++b) {
    size_t bit = f.bit_offset + b;
    size_t byte = bit >> 3;
    if (byte >= report_len) break;
    if (report[byte] & (1u << (bit & 7))) v |= 1u << b;
  }
  if (f.is_signed() && f.bit_size < 32 && (v & (1u << (f.bit_size - 1)))) {
    v |= ~((1u << f.bit_size) - 1);
  }
  return (int32_t)v;
}

void Descriptor::set_raw_value(const Field& f, uint8_t* report, size_t report_len, int32_t value) {
  for (int b = 0; b < f.bit_size && b < 32; ++b) {
    size_t bit = f.bit_offset + b;
    size_t byte = bit >> 3;
    if (byte >= report_len) break;
    uint8_t mask = 1u << (bit & 7);
    if ((uint32_t)value & (1u << b)) report[byte] |= mask; else report[byte] &= ~mask;
  }
}

// NUT's hidtypes: base exponents of the HID unit codes the power class uses,
// so that a Voltage with unit 0x00F0D121 and exponent 7 reads in whole volts.
static int base_exponent(uint32_t unit) {
  switch (unit) {
    case 0x00F0D121: return 7;   // Volt
    case 0x00100001: return -2;  // Ampere
    case 0x0000F001: return 0;   // Hertz
    case 0x00001001: return 0;   // second
    case 0x00010001: return 0;   // Kelvin
    case 0x0000D121: return 7;   // Watt
    case 0x00101001: return 0;   // Ampere-second
    case 0x0000E121: return 7;   // volt-ampere (rare)
    default: return 0;
  }
}

double Descriptor::physical_value(const Field& f, int32_t raw) {
  double v = raw;
  if ((f.phys_min != 0 || f.phys_max != 0) && f.log_max != f.log_min) {
    v = f.phys_min + (double)(raw - f.log_min) * (f.phys_max - f.phys_min) / (double)(f.log_max - f.log_min);
  }
  int e = f.unit_exp - base_exponent(f.unit);
  if (e) v *= pow(10.0, e);
  return v;
}

int Descriptor::format_path(const Field& f, char* out, size_t out_len) {
  char tmp[24];
  int pos = 0;
  for (int k = 0; k < f.path_len; ++k) {
    pos += snprintf(out + pos, pos < (int)out_len ? out_len - pos : 0, "%s%s", k ? "." : "", usage_name(f.path[k], tmp, sizeof tmp));
  }
  pos += snprintf(out + pos, pos < (int)out_len ? out_len - pos : 0, "%s%s", f.path_len ? "." : "", usage_name(f.usage, tmp, sizeof tmp));
  return pos;
}

// ---------------------------------------------------------------- names

struct Name { uint8_t id; const char* name; };

static const Name kPower84[] = {
  {0x01,"iName"},{0x02,"PresentStatus"},{0x03,"ChangedStatus"},{0x04,"UPS"},{0x05,"PowerSupply"},
  {0x10,"BatterySystem"},{0x11,"BatterySystemID"},{0x12,"Battery"},{0x13,"BatteryID"},{0x14,"Charger"},
  {0x15,"ChargerID"},{0x16,"PowerConverter"},{0x17,"PowerConverterID"},{0x18,"OutletSystem"},
  {0x19,"OutletSystemID"},{0x1a,"Input"},{0x1b,"InputID"},{0x1c,"Output"},{0x1d,"OutputID"},{0x1e,"Flow"},
  {0x1f,"FlowID"},{0x20,"Outlet"},{0x21,"OutletID"},{0x22,"Gang"},{0x23,"GangID"},{0x24,"PowerSummary"},
  {0x25,"PowerSummaryID"},{0x30,"Voltage"},{0x31,"Current"},{0x32,"Frequency"},{0x33,"ApparentPower"},
  {0x34,"ActivePower"},{0x35,"PercentLoad"},{0x36,"Temperature"},{0x37,"Humidity"},{0x38,"BadCount"},
  {0x40,"ConfigVoltage"},{0x41,"ConfigCurrent"},{0x42,"ConfigFrequency"},{0x43,"ConfigApparentPower"},
  {0x44,"ConfigActivePower"},{0x45,"ConfigPercentLoad"},{0x46,"ConfigTemperature"},{0x47,"ConfigHumidity"},
  {0x50,"SwitchOnControl"},{0x51,"SwitchOffControl"},{0x52,"ToggleControl"},{0x53,"LowVoltageTransfer"},
  {0x54,"HighVoltageTransfer"},{0x55,"DelayBeforeReboot"},{0x56,"DelayBeforeStartup"},
  {0x57,"DelayBeforeShutdown"},{0x58,"Test"},{0x59,"ModuleReset"},{0x5a,"AudibleAlarmControl"},
  {0x60,"Present"},{0x61,"Good"},{0x62,"InternalFailure"},{0x63,"VoltageOutOfRange"},
  {0x64,"FrequencyOutOfRange"},{0x65,"Overload"},{0x66,"OverCharged"},{0x67,"OverTemperature"},
  {0x68,"ShutdownRequested"},{0x69,"ShutdownImminent"},{0x6b,"SwitchOnOff"},{0x6c,"Switchable"},
  {0x6d,"Used"},{0x6e,"Boost"},{0x6f,"Buck"},{0x70,"Initialized"},{0x71,"Tested"},{0x72,"AwaitingPower"},
  {0x73,"CommunicationLost"},{0xfd,"iManufacturer"},{0xfe,"iProduct"},{0xff,"iSerialNumber"},
};
static const Name kBattery85[] = {
  {0x01,"SMBBatteryMode"},{0x02,"SMBBatteryStatus"},{0x03,"SMBAlarmWarning"},{0x04,"SMBChargerMode"},
  {0x05,"SMBChargerStatus"},{0x06,"SMBChargerSpecInfo"},{0x07,"SMBSelectorState"},{0x08,"SMBSelectorPresets"},
  {0x09,"SMBSelectorInfo"},{0x10,"OptionalMfgFunction1"},{0x11,"OptionalMfgFunction2"},
  {0x12,"OptionalMfgFunction3"},{0x13,"OptionalMfgFunction4"},{0x14,"OptionalMfgFunction5"},
  {0x15,"ConnectionToSMBus"},{0x16,"OutputConnection"},{0x17,"ChargerConnection"},{0x18,"BatteryInsertion"},
  {0x19,"Usenext"},{0x1a,"OKToUse"},{0x1b,"BatterySupported"},{0x1c,"SelectorRevision"},
  {0x1d,"ChargingIndicator"},{0x28,"ManufacturerAccess"},{0x29,"RemainingCapacityLimit"},
  {0x2a,"RemainingTimeLimit"},{0x2b,"AtRate"},{0x2c,"CapacityMode"},{0x2d,"BroadcastToCharger"},
  {0x2e,"PrimaryBattery"},{0x2f,"ChargeController"},{0x40,"TerminateCharge"},{0x41,"TerminateDischarge"},
  {0x42,"BelowRemainingCapacityLimit"},{0x43,"RemainingTimeLimitExpired"},{0x44,"Charging"},
  {0x45,"Discharging"},{0x46,"FullyCharged"},{0x47,"FullyDischarged"},{0x48,"ConditioningFlag"},
  {0x49,"AtRateOK"},{0x4a,"SMBErrorCode"},{0x4b,"NeedReplacement"},{0x60,"AtRateTimeToFull"},
  {0x61,"AtRateTimeToEmpty"},{0x62,"AverageCurrent"},{0x63,"Maxerror"},{0x64,"RelativeStateOfCharge"},
  {0x65,"AbsoluteStateOfCharge"},{0x66,"RemainingCapacity"},{0x67,"FullChargeCapacity"},
  {0x68,"RunTimeToEmpty"},{0x69,"AverageTimeToEmpty"},{0x6a,"AverageTimeToFull"},{0x6b,"CycleCount"},
  {0x80,"BattPackModelLevel"},{0x81,"InternalChargeController"},{0x82,"PrimaryBatterySupport"},
  {0x83,"DesignCapacity"},{0x84,"SpecificationInfo"},{0x85,"ManufacturerDate"},{0x86,"SerialNumber"},
  {0x87,"iManufacturerName"},{0x88,"iDevicename"},{0x89,"iDeviceChemistry"},{0x8a,"ManufacturerData"},
  {0x8b,"Rechargable"},{0x8c,"WarningCapacityLimit"},{0x8d,"CapacityGranularity1"},
  {0x8e,"CapacityGranularity2"},{0x8f,"iOEMInformation"},{0xc0,"InhibitCharge"},{0xc1,"EnablePolling"},
  {0xc2,"ResetToZero"},{0xd0,"ACPresent"},{0xd1,"BatteryPresent"},{0xd2,"PowerFail"},
  {0xd3,"AlarmInhibited"},{0xd4,"ThermistorUnderRange"},{0xd5,"ThermistorHot"},{0xd6,"ThermistorCold"},
  {0xd7,"ThermistorOverRange"},{0xd8,"VoltageOutOfRange"},{0xd9,"CurrentOutOfRange"},
  {0xda,"CurrentNotRegulated"},{0xdb,"VoltageNotRegulated"},{0xdc,"MasterMode"},
  {0xf0,"ChargerSelectorSupport"},{0xf1,"ChargerSpec"},{0xf2,"Level2"},{0xf3,"Level3"},
};
// APC vendor page, names as used by NUT's apc-hid.c where known.
static const Name kApcFF86[] = {
  {0x01,"APCCollection1"},{0x05,"APCGeneralCollection"},{0x06,"APCEnvironment"},{0x07,"APCProbe1"},
  {0x08,"APCProbe2"},{0x16,"APCBattReplaceDate"},{0x19,"APCBattCapBeforeStartup"},
  {0x42,"APC_UPS_FirmwareRevision"},{0x52,"APCLineFailCause"},{0x60,"APCStatusFlag"},
  {0x61,"APCSensitivity"},{0x72,"APCPanelTest"},{0x76,"APCShutdownAfterDelay"},
  {0x79,"APC_USB_FirmwareRevision"},{0x7c,"APCDelayBeforeReboot"},{0x7d,"APCDelayBeforeShutdown"},
  {0x7e,"APCDelayBeforeStartup"},
};

const char* page_name(uint16_t page) {
  switch (page) {
    case 0x84: return "PowerDevice";
    case 0x85: return "BatterySystem";
    case 0xFF86: return "APC";
    default: return "Page";
  }
}

const char* usage_name(uint32_t usage, char* buf, size_t buf_len) {
  uint16_t page = usage >> 16;
  uint8_t id = usage & 0xFF;
  const Name* tbl = nullptr; size_t n = 0;
  if (page == 0x84) { tbl = kPower84; n = sizeof kPower84 / sizeof kPower84[0]; }
  else if (page == 0x85) { tbl = kBattery85; n = sizeof kBattery85 / sizeof kBattery85[0]; }
  else if (page == 0xFF86) { tbl = kApcFF86; n = sizeof kApcFF86 / sizeof kApcFF86[0]; }
  if (tbl && (usage & 0xFF00) == 0) {
    for (size_t i = 0; i < n; ++i) if (tbl[i].id == id) return tbl[i].name;
  }
  if (page == 0xFF86) snprintf(buf, buf_len, "APC_%02X", (unsigned)(usage & 0xFFFF));
  else snprintf(buf, buf_len, "%04X_%04X", page, (unsigned)(usage & 0xFFFF));
  return buf;
}

}  // namespace hid
