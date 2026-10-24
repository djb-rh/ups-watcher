#include "ups_hid.h"
#include <SPI.h>
#include "M5_Max3421E_Usb.h"

UpsHid upsHid;
static M5_USBH_Host* g_host = nullptr;

// The MAX3421E raises FRAMEIRQ every millisecond while a device is attached and
// the TinyUSB driver services it over SPI each time, which the Core Basic's
// speaker amplifier turns into a 1 kHz whine. The frame interrupt is only
// needed to retry NAKed transfers, so keep it masked except while one of our
// transfers is in flight. HIEN (reg 26): CONDET 0x20 | FRAME 0x40 | HXFRDN 0x80 | RCVDAV 0x04.
static void frame_irq(bool on) {
  if (!g_host) return;
  g_host->max3421_writeRegister(26 << 3, on ? 0xE4 : 0xA4, false);
}

static void utf16_to_ascii(const uint8_t* buf, size_t buflen, char* out, size_t outlen) {
  size_t n = buf[0] < buflen ? buf[0] : buflen;
  size_t o = 0;
  for (size_t i = 2; i + 1 < n && o + 1 < outlen; i += 2) {
    uint16_t c = buf[i] | (buf[i + 1] << 8);
    out[o++] = (c < 0x80) ? (char)c : '?';
  }
  out[o] = 0;
  while (o && out[o - 1] == ' ') out[--o] = 0;
}

bool UpsHid::begin(int cs, int intr) {
  cs_ = cs; intr_ = intr;
  state_.set("driver.name", "ups-watcher", ups::F_STRING);
  state_.set("driver.version", UPSW_VERSION, ups::F_STRING);
  state_.set("driver.version.internal", UPSW_VERSION, ups::F_STRING);
  state_.set("driver.parameter.port", "usb", ups::F_STRING);
  state_.set("device.type", "ups", ups::F_STRING);
  if (cs < 0) return false;
  g_host = new M5_USBH_Host(&SPI, 18, 23, 19, cs, intr);
  if (!g_host->begin(0)) { Serial.println("[usb] host init failed"); return false; }
  Serial.printf("[usb] host started, CS %d INT %d\r\n", cs, intr);
  return true;
}

void UpsHid::set_extra_alarm(const char* text) {
  snprintf(extra_alarm_, sizeof extra_alarm_, "%s", text ? text : "");
  compose_status();
}

void UpsHid::compose_status() {
  char st[80], al[160];
  upsmap::build_status(flags_, charge_, st, sizeof st);
  if (offline_) snprintf(st, sizeof st, "OFF");
  upsmap::build_alarm(flags_, al, sizeof al);
  if (extra_alarm_[0]) {
    if (al[0]) strncat(al, " ", sizeof al - strlen(al) - 1);
    strncat(al, extra_alarm_, sizeof al - strlen(al) - 1);
  }
  if (al[0]) {
    char full[96];
    snprintf(full, sizeof full, "ALARM %s", st);
    state_.set("ups.status", full, ups::F_STRING);
    state_.set("ups.alarm", al, ups::F_STRING);
  } else {
    state_.set("ups.status", st, ups::F_STRING);
    state_.remove("ups.alarm");
  }
}

// ------------------------------------------------------------- binding

const hid::Field* UpsHid::find_feature_by_path(const char* path) {
  char p[160];
  for (int i = 0; i < desc_.size(); ++i) {
    const hid::Field& f = desc_[i];
    if (f.type != hid::R_FEATURE) continue;
    desc_.format_path(f, p, sizeof p);
    if (strcmp(p, path) == 0) return &f;
  }
  return nullptr;
}

void UpsHid::bind_fields() {
  n_vbinds_ = n_sbinds_ = n_reports_ = 0;
  char p[160];
  // Pass over every field once, matching paths against the maps.
  for (int i = 0; i < desc_.size(); ++i) {
    const hid::Field& f = desc_[i];
    if (f.type == hid::R_OUTPUT) continue;
    desc_.format_path(f, p, sizeof p);
    for (int m = 0; m < upsmap::kVarMapCount; ++m) {
      if (strcmp(p, upsmap::kVarMap[m].path) != 0) continue;
      const char* var = upsmap::kVarMap[m].var;
      // already bound to a higher-priority path?
      VarBind* b = nullptr;
      for (int k = 0; k < n_vbinds_; ++k) if (strcmp(vbinds_[k].e->var, var) == 0) { b = &vbinds_[k]; break; }
      if (b && b->e != &upsmap::kVarMap[m]) {
        // a different (earlier) path already owns this var; only allow the
        // input twin of the same path
        break;
      }
      if (!b) {
        if (n_vbinds_ >= (int)(sizeof vbinds_ / sizeof vbinds_[0])) break;
        b = &vbinds_[n_vbinds_++];
        b->e = &upsmap::kVarMap[m]; b->ffield = b->ifield = -1; b->last_raw = 0; b->string_done = false;
      }
      if (f.type == hid::R_FEATURE && b->ffield < 0) b->ffield = i;
      if (f.type == hid::R_INPUT && b->ifield < 0) b->ifield = i;
      break;
    }
    for (int m = 0; m < upsmap::kStatusMapCount; ++m) {
      if (strcmp(p, upsmap::kStatusMap[m].path) != 0) continue;
      StatBind* b = nullptr;
      for (int k = 0; k < n_sbinds_; ++k) if (sbinds_[k].flag == upsmap::kStatusMap[m].flag) { b = &sbinds_[k]; break; }
      if (!b) {
        if (n_sbinds_ >= (int)(sizeof sbinds_ / sizeof sbinds_[0])) break;
        b = &sbinds_[n_sbinds_++];
        b->flag = upsmap::kStatusMap[m].flag; b->inv = upsmap::kStatusMap[m].inverted; b->ffield = b->ifield = -1;
      } else {
        // keep the first (PresentStatus) path, unless this is its input twin
        const hid::Field& have = desc_[b->ffield >= 0 ? b->ffield : b->ifield];
        if (have.report_id != f.report_id || have.bit_offset != f.bit_offset) break;
      }
      if (f.type == hid::R_FEATURE && b->ffield < 0) b->ffield = i;
      if (f.type == hid::R_INPUT && b->ifield < 0) b->ifield = i;
      break;
    }
  }
  // Feature reports we need to poll.
  auto need = [&](int fi, bool semi) {
    if (fi < 0) return;
    const hid::Field& f = desc_[fi];
    RawReport* r = find_report(f.report_id);
    if (!r) {
      if (n_reports_ >= (int)(sizeof reports_ / sizeof reports_[0])) return;
      r = &reports_[n_reports_++];
      memset(r, 0, sizeof *r);
      r->id = f.report_id;
      r->len = desc_.report_bits(f.report_id, hid::R_FEATURE) / 8;
      if (r->len > 8) r->len = 8;
      r->semi = true;
    }
    if (!semi) r->semi = false;
  };
  for (int k = 0; k < n_vbinds_; ++k) need(vbinds_[k].ffield, vbinds_[k].e->flags & upsmap::M_SEMI);
  for (int k = 0; k < n_sbinds_; ++k) need(sbinds_[k].ffield, false);
  bool have_chg = false;
  for (int k = 0; k < n_sbinds_; ++k) if (sbinds_[k].flag == upsmap::S_CHRG || sbinds_[k].flag == upsmap::S_DISCHRG) have_chg = true;
  flags_ = have_chg ? upsmap::S_HAVE_CHG_FLAGS : 0;

  // Instant commands this device can take.
  state_.clear_cmds();
  for (int m = 0; m < upsmap::kCmdMapCount; ++m)
    if (find_feature_by_path(upsmap::kCmdMap[m].path)) state_.add_cmd(upsmap::kCmdMap[m].cmd);

  Serial.printf("[usb] bound %d variables, %d status flags, %d feature reports, %d commands\r\n",
                n_vbinds_, n_sbinds_, n_reports_, state_.cmd_count());
}

UpsHid::RawReport* UpsHid::find_report(uint8_t rid) {
  for (int i = 0; i < n_reports_; ++i) if (reports_[i].id == rid) return &reports_[i];
  return nullptr;
}

// ------------------------------------------------------------- decoding

void UpsHid::apply_var(VarBind& b, const hid::Field& f, int32_t raw) {
  const upsmap::VarEntry& e = *b.e;
  b.last_raw = raw;
  char buf[64];
  uint8_t fl = (e.flags & upsmap::M_RW) ? ups::F_RW : 0;
  double phys = hid::Descriptor::physical_value(f, raw);
  switch (e.conv) {
    case upsmap::C_NUM:
      if (strcmp(e.var, "battery.charge") == 0) charge_ = (int)phys;
      state_.set_num(e.var, phys, e.decimals, fl);
      break;
    case upsmap::C_STRINGID:
      // resolved later by fetch_strings() (needs a blocking control transfer)
      if (raw > 0 && raw < 256 && !b.string_done) strings_pending_ = true;
      break;
    case upsmap::C_DATE: upsmap::hid_date_str(raw, buf, sizeof buf); state_.set(e.var, buf, ups::F_STRING | fl); break;
    case upsmap::C_APCDATE: upsmap::apc_date_str(raw, buf, sizeof buf); state_.set(e.var, buf, ups::F_STRING | fl); break;
    case upsmap::C_KELVIN: state_.set_num(e.var, phys - 273.15, e.decimals, fl); break;
    case upsmap::C_BEEPER: state_.set(e.var, upsmap::beeper_str(raw), ups::F_STRING | fl); break;
    case upsmap::C_TESTRES: state_.set(e.var, upsmap::test_result_str(raw), ups::F_STRING | fl); break;
    case upsmap::C_SENS: state_.set(e.var, upsmap::sensitivity_str(raw), ups::F_STRING | fl); break;
    case upsmap::C_LINEFAIL: state_.set(e.var, upsmap::linefail_str(raw), ups::F_STRING | fl); break;
  }
}

void UpsHid::decode(uint8_t type, uint8_t rid, const uint8_t* payload, size_t plen) {
  bool status_touched = false;
  for (int k = 0; k < n_vbinds_; ++k) {
    int fi = type == hid::R_FEATURE ? vbinds_[k].ffield : vbinds_[k].ifield;
    if (fi < 0) continue;
    const hid::Field& f = desc_[fi];
    if (f.report_id != rid) continue;
    apply_var(vbinds_[k], f, hid::Descriptor::raw_value(f, payload, plen));
  }
  for (int k = 0; k < n_sbinds_; ++k) {
    int fi = type == hid::R_FEATURE ? sbinds_[k].ffield : sbinds_[k].ifield;
    if (fi < 0) continue;
    const hid::Field& f = desc_[fi];
    if (f.report_id != rid) continue;
    bool v = hid::Descriptor::raw_value(f, payload, plen) != 0;
    if (sbinds_[k].inv) v = !v;
    if (v) flags_ |= sbinds_[k].flag; else flags_ &= ~sbinds_[k].flag;
    status_touched = true;
  }
  if (status_touched) compose_status();
}

// ------------------------------------------------------------- strings

void UpsHid::fetch_device_strings() {
  dev_strings_pending_ = false;
  uint8_t sbuf[160];
  frame_irq(true);
  if (tuh_descriptor_get_manufacturer_string_sync(daddr_, 0x0409, sbuf, sizeof sbuf) == XFER_RESULT_SUCCESS)
    utf16_to_ascii(sbuf, sizeof sbuf, mfr_, sizeof mfr_);
  if (tuh_descriptor_get_product_string_sync(daddr_, 0x0409, sbuf, sizeof sbuf) == XFER_RESULT_SUCCESS)
    utf16_to_ascii(sbuf, sizeof sbuf, product_, sizeof product_);
  if (tuh_descriptor_get_serial_string_sync(daddr_, 0x0409, sbuf, sizeof sbuf) == XFER_RESULT_SUCCESS)
    utf16_to_ascii(sbuf, sizeof sbuf, serial_, sizeof serial_);
  char model[64], fw[32], fwaux[32];
  upsmap::split_apc_product(product_, model, sizeof model, fw, sizeof fw, fwaux, sizeof fwaux);
  if (mfr_[0]) { state_.set("ups.mfr", mfr_, ups::F_STRING); state_.set("device.mfr", mfr_, ups::F_STRING); }
  if (model[0]) { state_.set("ups.model", model, ups::F_STRING); state_.set("device.model", model, ups::F_STRING); }
  if (serial_[0]) { state_.set("ups.serial", serial_, ups::F_STRING); state_.set("device.serial", serial_, ups::F_STRING); }
  if (fw[0]) state_.set("ups.firmware", fw, ups::F_STRING);
  if (fwaux[0]) state_.set("ups.firmware.aux", fwaux, ups::F_STRING);
  frame_irq(false);
  Serial.printf("[usb] %s / %s / %s\r\n", mfr_, product_, serial_);
  char ev[120];
  snprintf(ev, sizeof ev, "UPS connected: %s (%s)", model[0] ? model : product_, serial_);
  if (on_event) on_event(ev);
}

void UpsHid::fetch_strings() {
  strings_pending_ = false;
  uint8_t sbuf[160];
  char s[64];
  frame_irq(true);
  for (int k = 0; k < n_vbinds_; ++k) {
    VarBind& b = vbinds_[k];
    if (b.e->conv != upsmap::C_STRINGID || b.string_done || b.last_raw <= 0 || b.last_raw > 255) continue;
    if (tuh_descriptor_get_string_sync(daddr_, (uint8_t)b.last_raw, 0x0409, sbuf, sizeof sbuf) != XFER_RESULT_SUCCESS) continue;
    utf16_to_ascii(sbuf, sizeof sbuf, s, sizeof s);
    b.string_done = true;
    if (strcmp(b.e->var, "ups.model") == 0) {
      char model[64], fw[32], fwaux[32];
      upsmap::split_apc_product(s, model, sizeof model, fw, sizeof fw, fwaux, sizeof fwaux);
      if (!state_.has("ups.model")) { state_.set("ups.model", model, ups::F_STRING); state_.set("device.model", model, ups::F_STRING); }
      if (fw[0] && !state_.has("ups.firmware")) state_.set("ups.firmware", fw, ups::F_STRING);
      if (fwaux[0] && !state_.has("ups.firmware.aux")) state_.set("ups.firmware.aux", fwaux, ups::F_STRING);
    } else if (strcmp(b.e->var, "ups.mfr") == 0 || strcmp(b.e->var, "ups.serial") == 0) {
      if (!state_.has(b.e->var)) state_.set(b.e->var, s, ups::F_STRING);
    } else {
      state_.set(b.e->var, s, ups::F_STRING);
    }
  }
  frame_irq(false);
}

// ------------------------------------------------------------- polling

void UpsHid::start_next_poll() {
  if (n_reports_ == 0) return;
  uint32_t now = millis();
  for (int tries = 0; tries < n_reports_; ++tries) {
    RawReport& r = reports_[poll_idx_];
    poll_idx_ = (poll_idx_ + 1) % n_reports_;
    bool wrapped = poll_idx_ == 0;
    bool skip = false;
    if (r.semi && r.ok && (cycle_ % 12) != 0) skip = true;              // semi-static: every 12th cycle
    if (r.fails >= 3 && (int32_t)(now - r.retry_ms) < 0) skip = true;   // broken report: back off
    if (pending_reread_ == r.id) { skip = false; pending_reread_ = 0; }
    if (wrapped) {
      cycle_++;
      next_poll_ms_ = now + poll_ms_;
      if (!data_ok_) { data_ok_ = true; if (on_event) on_event("UPS data complete, serving NUT clients"); }
      last_ok_ms_ = now;
      state_.set_stale(false);
      compose_status();
    }
    if (skip) { if (wrapped) return; continue; }
    memset(rbuf_, 0, sizeof rbuf_);
    frame_irq(true);
    if (tuh_hid_get_report(daddr_, idx_, r.id, HID_REPORT_TYPE_FEATURE, rbuf_, r.len + 1)) {
      busy_ = true; busy_since_ = now; polls_++;
    } else {
      r.fails++;
      frame_irq(false);
    }
    return;
  }
}

void UpsHid::task() {
  if (!g_host) return;
  g_host->task();
  if (!mounted_ || !have_desc_) return;
  uint32_t now = millis();
  if (dev_strings_pending_) { fetch_device_strings(); return; }
  if (busy_ && now - busy_since_ > 1500) {
    busy_ = false;  // lost completion; move on
    frame_irq(false);
    Serial.println("[usb] report timeout");
  }
  if (busy_) return;
  if (strings_pending_) { fetch_strings(); return; }
  bool mid_cycle = poll_idx_ != 0;
  if (mid_cycle || (int32_t)(now - next_poll_ms_) >= 0) start_next_poll();
}

bool UpsHid::wait_idle(uint32_t ms) {
  uint32_t t0 = millis();
  while (busy_ && millis() - t0 < ms) { g_host->task(); delay(1); }
  return !busy_;
}

bool UpsHid::write_feature(const hid::Field& f, int32_t value) {
  if (!mounted_ || !have_desc_) return false;
  if (!wait_idle(800)) return false;
  RawReport* r = find_report(f.report_id);
  uint8_t len = r ? r->len : desc_.report_bits(f.report_id, hid::R_FEATURE) / 8;
  if (len > 8) len = 8;
  uint8_t buf[9] = {0};
  buf[0] = f.report_id;
  if (r && r->ok) memcpy(buf + 1, r->data, len);
  hid::Descriptor::set_raw_value(f, buf + 1, len, value);
  set_in_flight_ = true; set_ok_ = false; busy_ = true; busy_since_ = millis();
  frame_irq(true);
  if (!tuh_hid_set_report(daddr_, idx_, f.report_id, HID_REPORT_TYPE_FEATURE, buf, len + 1)) {
    busy_ = false; set_in_flight_ = false; frame_irq(false); return false;
  }
  uint32_t t0 = millis();
  while (set_in_flight_ && millis() - t0 < 800) { g_host->task(); delay(1); }
  busy_ = false;
  frame_irq(false);
  bool ok = !set_in_flight_ && set_ok_;
  set_in_flight_ = false;
  pending_reread_ = f.report_id;
  if (r) r->ok = false;
  return ok;
}

bool UpsHid::instcmd(const char* cmd) {
  for (int m = 0; m < upsmap::kCmdMapCount; ++m) {
    if (strcmp(upsmap::kCmdMap[m].cmd, cmd) != 0) continue;
    const hid::Field* f = find_feature_by_path(upsmap::kCmdMap[m].path);
    if (!f) continue;
    bool ok = write_feature(*f, upsmap::kCmdMap[m].value);
    char ev[96];
    snprintf(ev, sizeof ev, "Command %s %s", cmd, ok ? "sent" : "FAILED");
    if (on_event) on_event(ev);
    return ok;
  }
  return false;
}

bool UpsHid::setvar(const char* var, const char* value) {
  for (int m = 0; m < upsmap::kSetMapCount; ++m) {
    if (strcmp(upsmap::kSetMap[m].var, var) != 0) continue;
    const hid::Field* f = find_feature_by_path(upsmap::kSetMap[m].path);
    if (!f) continue;
    long v;
    if (upsmap::kSetMap[m].conv == upsmap::C_SENS) v = upsmap::sensitivity_value(value);
    else v = atol(value);
    if (v < f->log_min || v > f->log_max) return false;
    bool ok = write_feature(*f, (int32_t)v);
    char ev[96];
    snprintf(ev, sizeof ev, "Set %s=%s %s", var, value, ok ? "ok" : "FAILED");
    if (on_event) on_event(ev);
    return ok;
  }
  return false;
}

// ------------------------------------------------------------- callbacks

void UpsHid::_mount(uint8_t daddr, uint8_t idx, const uint8_t* desc, uint16_t len) {
  daddr_ = daddr; idx_ = idx;
  tuh_vid_pid_get(daddr, &vid_, &pid_);
  tuh_itf_info_t info;
  tuh_hid_itf_get_info(daddr, idx, &info);
  itf_num_ = info.desc.bInterfaceNumber;
  mounted_ = true; have_desc_ = false; data_ok_ = false; offline_ = false;
  mounted_ms_ = millis();
  busy_ = false; poll_idx_ = 0; cycle_ = 0; next_poll_ms_ = 0;
  char b[8];
  snprintf(b, sizeof b, "%04x", vid_); state_.set("ups.vendorid", b, ups::F_STRING);
  snprintf(b, sizeof b, "%04x", pid_); state_.set("ups.productid", b, ups::F_STRING);
  Serial.printf("[usb] HID mounted VID %04X PID %04X, descriptor %u bytes\r\n", vid_, pid_, len);
  if (len) {
    memcpy(desc_buf_, desc, len > sizeof desc_buf_ ? sizeof desc_buf_ : len);
    _desc_done(true, len);
  } else {
    if (!tuh_descriptor_get_hid_report(daddr, itf_num_, HID_DESC_TYPE_REPORT, 0, desc_buf_, sizeof desc_buf_,
                                       [](tuh_xfer_t* x) { upsHid._desc_done(x->result == XFER_RESULT_SUCCESS, x->actual_len); }, 0)) {
      Serial.println("[usb] get report descriptor refused");
    }
  }
  // Interrupt-IN polling deliberately not armed: a pending IN transfer is
  // retried every frame and that 1 kHz SPI traffic is audible on the Core.
}

void UpsHid::_desc_done(bool ok, uint32_t len) {
  if (!ok) { Serial.println("[usb] report descriptor fetch failed"); return; }
  desc_len_ = len > sizeof desc_buf_ ? sizeof desc_buf_ : len;
  desc_.parse(desc_buf_, desc_len_);
  Serial.printf("[usb] report descriptor %u bytes, %d fields\r\n", desc_len_, desc_.size());
  bind_fields();
  have_desc_ = true;
  dev_strings_pending_ = true;
  frame_irq(false);
}

void UpsHid::_umount() {
  Serial.println("[usb] unmounted");
  frame_irq(true);  // the next enumeration needs NAK retries
  mounted_ = false; have_desc_ = false; busy_ = false; strings_pending_ = false; dev_strings_pending_ = false;
  if (on_event) on_event("UPS disconnected from USB");
}

void UpsHid::_input_report(const uint8_t* rep, uint16_t len) {
  inputs_++;
  if (have_desc_ && len > 1) decode(hid::R_INPUT, rep[0], rep + 1, len - 1);
}

void UpsHid::_get_report_done(uint8_t rid, uint16_t len) {
  busy_ = false;
  frame_irq(false);
  RawReport* r = find_report(rid);
  if (len == 0) {
    if (r) { r->fails++; if (r->fails >= 3) r->retry_ms = millis() + 5 * 60 * 1000; }
    return;
  }
  const uint8_t* payload = rbuf_ + 1;
  size_t plen = len - 1;
  if (r) {
    r->fails = 0; r->ok = true; r->ms = millis();
    r->len = plen > 8 ? 8 : plen;
    memcpy(r->data, payload, r->len);
  }
  decode(hid::R_FEATURE, rid, payload, plen);
}

void UpsHid::_set_report_done(uint8_t rid, uint16_t len) {
  (void)rid;
  set_ok_ = len > 0;
  set_in_flight_ = false;
}

extern "C" {
void tuh_mount_cb(uint8_t daddr) { (void)daddr; }
void tuh_umount_cb(uint8_t daddr) { (void)daddr; upsHid._umount(); }
void tuh_hid_mount_cb(uint8_t daddr, uint8_t idx, uint8_t const* desc_report, uint16_t desc_len) {
  upsHid._mount(daddr, idx, desc_report, desc_len);
}
void tuh_hid_umount_cb(uint8_t daddr, uint8_t idx) { (void)daddr; (void)idx; }
void tuh_hid_report_received_cb(uint8_t daddr, uint8_t idx, uint8_t const* report, uint16_t len) {
  (void)daddr; (void)idx; upsHid._input_report(report, len);
}
void tuh_hid_get_report_complete_cb(uint8_t daddr, uint8_t idx, uint8_t report_id, uint8_t report_type, uint16_t len) {
  (void)daddr; (void)idx; (void)report_type; upsHid._get_report_done(report_id, len);
}
void tuh_hid_set_report_complete_cb(uint8_t daddr, uint8_t idx, uint8_t report_id, uint8_t report_type, uint16_t len) {
  (void)daddr; (void)idx; (void)report_type; upsHid._set_report_done(report_id, len);
}
}
