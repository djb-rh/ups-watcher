// USB HID Power Device driver on top of the TinyUSB host (MAX3421E).
// Enumerates the UPS, fetches and parses its report descriptor, binds the
// NUT variable map to the fields it actually has, then polls Feature reports
// and listens to Input reports, keeping an ups::State up to date.
#pragma once
#include <Arduino.h>
#include <functional>
#include "hid_parser.h"
#include "ups_state.h"
#include "ups_hid_map.h"

class UpsHid {
 public:
  struct RawReport {
    uint8_t id;
    uint8_t len;        // payload bytes (without ID)
    uint8_t data[8];    // payload
    uint32_t ms;        // millis() of last successful read
    uint8_t fails;      // consecutive failures
    uint32_t retry_ms;  // don't retry before this
    bool semi;          // semi-static: polled rarely
    bool ok;            // read at least once
  };

  bool begin(int cs, int intr);
  void task();

  bool connected() const { return mounted_; }
  bool have_descriptor() const { return have_desc_; }
  bool data_ok() const { return data_ok_; }
  uint32_t last_ok_ms() const { return last_ok_ms_; }
  uint32_t mounted_ms() const { return mounted_ms_; }
  uint32_t flags() const { return flags_; }
  int charge() const { return charge_; }
  ups::State& state() { return state_; }
  const hid::Descriptor& descriptor() const { return desc_; }
  uint16_t desc_len() const { return desc_len_; }
  const uint8_t* desc_bytes() const { return desc_buf_; }
  uint16_t vid() const { return vid_; }
  uint16_t pid() const { return pid_; }
  const char* product() const { return product_; }
  const char* manufacturer() const { return mfr_; }
  const char* serial() const { return serial_; }
  int cs_pin() const { return cs_; }
  int int_pin() const { return intr_; }
  uint32_t poll_count() const { return polls_; }
  uint32_t input_count() const { return inputs_; }

  void set_poll_interval(uint32_t ms) { poll_ms_ = ms < 1000 ? 1000 : ms; }
  bool instcmd(const char* cmd);
  bool setvar(const char* var, const char* value);

  int report_count() const { return n_reports_; }
  const RawReport& report(int i) const { return reports_[i]; }

  // App-level alarm text folded into ups.alarm / ALARM, and the OFF override
  // used when the UPS has vanished.
  void set_extra_alarm(const char* text);
  void set_offline(bool off) { offline_ = off; compose_status(); }
  void compose_status();

  std::function<void(const char*)> on_event;

  // TinyUSB callback plumbing (public for the C shims)
  void _mount(uint8_t daddr, uint8_t idx, const uint8_t* desc, uint16_t len);
  void _umount();
  void _input_report(const uint8_t* rep, uint16_t len);
  void _get_report_done(uint8_t rid, uint16_t len);
  void _set_report_done(uint8_t rid, uint16_t len);
  void _desc_done(bool ok, uint32_t len);
  uint8_t* _desc_buf() { return desc_buf_; }
  uint8_t* _report_buf() { return rbuf_; }

 private:
  struct VarBind {
    const upsmap::VarEntry* e;
    int16_t ffield, ifield;  // descriptor field indexes (feature / input), -1 if none
    int32_t last_raw;
    bool string_done;
  };
  struct StatBind {
    uint32_t flag;
    bool inv;
    int16_t ffield, ifield;
  };

  void bind_fields();
  void decode(uint8_t type, uint8_t rid, const uint8_t* payload, size_t plen);
  void apply_var(VarBind& b, const hid::Field& f, int32_t raw);
  void fetch_strings();
  void fetch_device_strings();
  void start_next_poll();
  RawReport* find_report(uint8_t rid);
  bool write_feature(const hid::Field& f, int32_t value);
  bool wait_idle(uint32_t ms);
  const hid::Field* find_feature_by_path(const char* path);

  hid::Descriptor desc_;
  uint8_t desc_buf_[1536];
  uint16_t desc_len_ = 0;
  uint8_t rbuf_[16];
  ups::State state_;
  VarBind vbinds_[48];
  int n_vbinds_ = 0;
  StatBind sbinds_[24];
  int n_sbinds_ = 0;
  RawReport reports_[64];
  int n_reports_ = 0;
  int poll_idx_ = 0;
  uint32_t cycle_ = 0;
  uint32_t poll_ms_ = 5000;
  uint32_t next_poll_ms_ = 0, busy_since_ = 0, last_ok_ms_ = 0, mounted_ms_ = 0;
  uint32_t polls_ = 0, inputs_ = 0;
  uint32_t flags_ = 0;
  int charge_ = -1;
  uint8_t daddr_ = 0, idx_ = 0, itf_num_ = 0;
  uint16_t vid_ = 0, pid_ = 0;
  char product_[72] = "", mfr_[48] = "", serial_[32] = "";
  char extra_alarm_[96] = "";
  bool mounted_ = false, have_desc_ = false, data_ok_ = false, offline_ = false;
  volatile bool busy_ = false;
  bool strings_pending_ = false, dev_strings_pending_ = false;
  bool set_in_flight_ = false;
  bool set_ok_ = false;
  uint8_t pending_reread_ = 0;
  int cs_ = -1, intr_ = -1;
};

extern UpsHid upsHid;
