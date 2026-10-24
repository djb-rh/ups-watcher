// HID report-descriptor parser tuned for the USB Power Device class (usage
// pages 0x84 Power Device, 0x85 Battery System, 0xFF86 APC vendor).
// Plain C++17, no Arduino dependencies, so it also builds in the native tests.
#pragma once
#include <cstdint>
#include <cstddef>

namespace hid {

enum ReportType : uint8_t { R_INPUT = 0, R_OUTPUT = 1, R_FEATURE = 2 };

static constexpr int MAX_PATH = 8;
static constexpr int MAX_FIELDS = 256;

// One usage inside one report. For a Report Count > 1 main item every element
// gets its own Field with its own bit offset.
struct Field {
  uint8_t  report_id;
  uint8_t  type;        // ReportType
  uint16_t bit_offset;  // offset of this element inside the report (after the ID byte)
  uint8_t  bit_size;
  uint8_t  path_len;    // enclosing collections, outermost first
  uint32_t path[MAX_PATH];  // (usage_page << 16) | usage
  uint32_t usage;           // (usage_page << 16) | usage
  int32_t  log_min, log_max;
  int32_t  phys_min, phys_max;
  uint32_t unit;
  int8_t   unit_exp;
  bool     is_variable;  // false = array (selector) item
  bool     is_constant;  // padding
  uint8_t  flags;        // raw main-item flags byte 0

  bool is_signed() const { return log_min < 0; }
};

struct ReportInfo {
  uint8_t  report_id;
  uint8_t  type;
  uint16_t bits;  // payload bits, excluding the report-ID byte
  uint16_t bytes() const { return (bits + 7) / 8; }
};

class Descriptor {
 public:
  // Parses `len` bytes of report descriptor. Returns false on a malformed
  // descriptor (fields parsed so far stay valid).
  bool parse(const uint8_t* data, size_t len);

  int size() const { return count_; }
  const Field& operator[](int i) const { return fields_[i]; }

  // Find the first field whose usage matches and whose collection path
  // ends with `path` (path given as (page<<16|usage) entries, outermost
  // first, may be shorter than the field's full path). type < 0 = any.
  const Field* find(uint32_t usage, const uint32_t* path, int path_len, int type = -1) const;

  // All report IDs seen, by type.
  int reports(uint8_t type, ReportInfo* out, int max) const;
  // Payload size in bits of (report_id,type), or 0 if unknown.
  uint16_t report_bits(uint8_t report_id, uint8_t type) const;

  // Raw logical value of a field from a report buffer (buffer begins AFTER the
  // report-ID byte). Sign-extends when the field is signed.
  static int32_t raw_value(const Field& f, const uint8_t* report, size_t report_len);
  // Physical value: applies the physical min/max mapping and 10^exponent
  // unit scaling the way NUT's usbhid-ups does (volts, amps, seconds, Hz...).
  static double physical_value(const Field& f, int32_t raw);
  // Writes a logical value back into a report buffer.
  static void set_raw_value(const Field& f, uint8_t* report, size_t report_len, int32_t value);

  // Human-readable "UPS.PowerSummary.PresentStatus.ACPresent"-style path.
  static int format_path(const Field& f, char* out, size_t out_len);

 private:
  Field fields_[MAX_FIELDS];
  int count_ = 0;
  struct Size { uint8_t id, type; uint16_t bits; };
  Size sizes_[64];
  int size_count_ = 0;
  uint16_t take_bits(uint8_t id, uint8_t type, uint16_t nbits);
};

// Usage names for pages 0x84, 0x85 and 0xFF86. Returns "0xNN" in `buf` for
// unknown usages.
const char* usage_name(uint32_t usage, char* buf, size_t buf_len);
const char* page_name(uint16_t page);

}  // namespace hid
