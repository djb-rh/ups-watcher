// HID Power Device paths -> NUT variables, mirroring NUT's usbhid-ups +
// apc-hid.c. Plain C++17 (no Arduino) so it can be unit-tested.
#pragma once
#include <cstdint>
#include <cstddef>

namespace upsmap {

enum Conv : uint8_t {
  C_NUM,       // numeric with `decimals`
  C_STRINGID,  // value is a USB string descriptor index
  C_DATE,      // HID packed date (year-1980)<<9 | month<<5 | day
  C_APCDATE,   // APC BCD mmddyy
  C_KELVIN,    // Kelvin -> Celsius
  C_BEEPER,    // AudibleAlarmControl 1/2/3
  C_TESTRES,   // Battery.Test result
  C_SENS,      // APCSensitivity
  C_LINEFAIL,  // APCLineFailCause
};

enum : uint8_t { M_RW = 1, M_SEMI = 2 /* poll rarely */ };

struct VarEntry {
  const char* path;
  const char* var;
  Conv conv;
  uint8_t decimals;
  uint8_t flags;
};

// Status bits (NUT's status_info names)
enum StatusFlag : uint32_t {
  S_ONLINE = 1u << 0, S_DISCHRG = 1u << 1, S_CHRG = 1u << 2, S_LOWBATT = 1u << 3,
  S_OVERLOAD = 1u << 4, S_REPLACEBATT = 1u << 5, S_SHUTDOWNIMM = 1u << 6,
  S_TIMELIMITEXP = 1u << 7, S_NOBATTERY = 1u << 8, S_FULLYCHARGED = 1u << 9,
  S_COMMFAULT = 1u << 10, S_BOOST = 1u << 11, S_TRIM = 1u << 12,
  S_AWAITINGPOWER = 1u << 13, S_OFF = 1u << 14, S_OVERHEAT = 1u << 15,
  S_VRANGE = 1u << 16, S_FRANGE = 1u << 17, S_BYPASS = 1u << 18,
  // which boolean sources were actually seen, so CHRG can fall back to charge%
  S_HAVE_CHG_FLAGS = 1u << 30,
};

struct StatusEntry {
  const char* path;
  uint32_t flag;
  bool inverted;  // set the flag when the HID value is 0
};

struct CmdEntry {
  const char* cmd;
  const char* path;
  int32_t value;
};

struct SetEntry {
  const char* var;
  const char* path;
  Conv conv;  // how to turn the string back into a raw value
};

extern const VarEntry kVarMap[];
extern const int kVarMapCount;
extern const StatusEntry kStatusMap[];
extern const int kStatusMapCount;
extern const CmdEntry kCmdMap[];
extern const int kCmdMapCount;
extern const SetEntry kSetMap[];
extern const int kSetMapCount;

// Builds "OL CHRG" etc. the way usbhid-ups does. `charge` < 0 if unknown.
void build_status(uint32_t flags, int charge, char* out, size_t n);
// Alarm text for the flags ("Replace battery!"), empty when none.
void build_alarm(uint32_t flags, char* out, size_t n);

const char* beeper_str(long v);
const char* test_result_str(long v);
const char* sensitivity_str(long v);
const char* linefail_str(long v);
long sensitivity_value(const char* s);  // -1 if unknown
void hid_date_str(long v, char* out, size_t n);
void apc_date_str(long v, char* out, size_t n);

// APC puts firmware revisions into the product string:
// "Back-UPS RS 700G FW:856.L8 -P.D USB FW:L8 -P " -> model / fw / fw.aux
void split_apc_product(const char* product, char* model, size_t ml, char* fw, size_t fl, char* fwaux, size_t al);

}  // namespace upsmap
