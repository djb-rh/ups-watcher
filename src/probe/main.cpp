// UPS Watcher — probe firmware.
// Enumerates whatever is on the USB Module, dumps the HID report descriptor,
// parses it with hid::Descriptor and polls every Feature report so the real
// APC usage layout can be read off the serial console. Also exercises the
// shared SPI bus (display + MAX3421E) the way the app will.
#include <Arduino.h>
#include <M5Unified.h>
#include <SPI.h>
#include "M5_Max3421E_Usb.h"
#include "hid_parser.h"

// Core Basic M5-Bus: SCK 18, MOSI 23, MISO 19; USB Module CS 5, INT 35.
static M5_USBH_Host* pUSBHost = nullptr;
#define USBHost (*pUSBHost)
#ifndef USBW_INT_PIN
#define USBW_INT_PIN 35
#endif

// The MAX3421E interrupt is serviced from its own FreeRTOS task over the same
// VSPI bus the LCD uses, so every LCD write holds the Arduino SPI mutex.
// M5GFX takes the Arduino SPI HAL's per-bus mutex (spiSimpleTransaction) for
// every draw, and the TinyUSB host takes the same mutex via SPI.beginTransaction,
// so no extra locking is needed. Kept as a no-op marker.
struct LcdLock { LcdLock() {} ~LcdLock() {} };

static uint8_t desc_buf[2048];
static hid::Descriptor desc;
static uint8_t g_daddr = 0, g_idx = 0;
static volatile bool g_mounted = false, g_have_desc = false, g_strings_done = false;
static volatile bool g_busy = false;
static uint8_t report_buf[66];
static hid::ReportInfo feats[64];
static int n_feats = 0, feat_i = 0;
static uint32_t next_poll = 0;
static char mfr[64], prod[64], serial_s[64];

static void lcd_status(const char* line1, const char* line2 = nullptr) {
  LcdLock lk;
  M5.Display.fillRect(0, 60, 320, 180, TFT_BLACK);
  M5.Display.setCursor(4, 70);
  M5.Display.setTextSize(2);
  M5.Display.println(line1);
  if (line2) M5.Display.println(line2);
}

static void hexdump(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    Serial.printf("%02X%s", p[i], ((i & 15) == 15 || i + 1 == n) ? "\r\n" : " ");
  }
}

static void utf16_to_ascii(const uint8_t* buf, size_t buflen, char* out, size_t outlen) {
  // buf[0] = bLength, buf[1] = type, then UTF-16LE
  size_t n = buf[0] < buflen ? buf[0] : buflen;
  size_t o = 0;
  for (size_t i = 2; i + 1 < n && o + 1 < outlen; i += 2) {
    uint16_t c = buf[i] | (buf[i + 1] << 8);
    out[o++] = (c < 0x80) ? (char)c : '?';
  }
  out[o] = 0;
}

static void dump_fields() {
  char path[160];
  Serial.printf("Parsed %d fields\r\n", desc.size());
  Serial.println("  RID  T  off  sz  lmin..lmax  pmin..pmax  unit      exp  path");
  for (int i = 0; i < desc.size(); ++i) {
    const hid::Field& f = desc[i];
    desc.format_path(f, path, sizeof path);
    Serial.printf("  %3u  %c  %3u  %2u  %5ld..%-6ld %5ld..%-6ld %08lX  %3d  %s%s\r\n",
                  f.report_id, "IOF"[f.type], f.bit_offset, f.bit_size,
                  (long)f.log_min, (long)f.log_max, (long)f.phys_min, (long)f.phys_max,
                  (unsigned long)f.unit, f.unit_exp, path, f.is_variable ? "" : " [array]");
  }
  n_feats = desc.reports(hid::R_FEATURE, feats, 64);
  Serial.printf("%d feature reports:", n_feats);
  for (int i = 0; i < n_feats; ++i) Serial.printf(" %u(%ub)", feats[i].report_id, feats[i].bytes());
  Serial.println();
  hid::ReportInfo ins[32];
  int ni = desc.reports(hid::R_INPUT, ins, 32);
  Serial.printf("%d input reports:", ni);
  for (int i = 0; i < ni; ++i) Serial.printf(" %u(%ub)", ins[i].report_id, ins[i].bytes());
  Serial.println();
}

static void on_report_desc(tuh_xfer_t* xfer) {
  if (xfer->result != XFER_RESULT_SUCCESS) {
    Serial.printf("GET_DESCRIPTOR(report) failed: %d\r\n", xfer->result);
    return;
  }
  Serial.printf("Report descriptor: %lu bytes\r\n", (unsigned long)xfer->actual_len);
  hexdump(desc_buf, xfer->actual_len);
  if (!desc.parse(desc_buf, xfer->actual_len)) Serial.println("(parse hit a malformed item)");
  dump_fields();
  g_have_desc = true;
}

extern "C" {

void tuh_mount_cb(uint8_t daddr) {
  uint16_t vid, pid;
  tuh_vid_pid_get(daddr, &vid, &pid);
  Serial.printf("USB device %u mounted VID %04X PID %04X\r\n", daddr, vid, pid);
}
void tuh_umount_cb(uint8_t daddr) {
  Serial.printf("USB device %u unmounted\r\n", daddr);
  g_mounted = false; g_have_desc = false; g_strings_done = false; g_busy = false;
  lcd_status("UPS unplugged");
}

void tuh_hid_mount_cb(uint8_t daddr, uint8_t idx, uint8_t const* desc_report, uint16_t desc_len) {
  uint16_t vid, pid;
  tuh_vid_pid_get(daddr, &vid, &pid);
  tuh_itf_info_t info;
  tuh_hid_itf_get_info(daddr, idx, &info);
  Serial.printf("HID itf mounted: daddr %u idx %u itf_num %u proto %u, descriptor %u bytes%s\r\n",
                daddr, idx, info.desc.bInterfaceNumber, info.desc.bInterfaceProtocol, desc_len,
                desc_len ? "" : " (too big for the stack's buffer, fetching it myself)");
  g_daddr = daddr; g_idx = idx; g_mounted = true;
  char l2[40];
  snprintf(l2, sizeof l2, "VID %04X PID %04X", vid, pid);
  lcd_status("HID device mounted", l2);
  if (desc_len) {
    memcpy(desc_buf, desc_report, desc_len);
    Serial.printf("Report descriptor: %u bytes\r\n", desc_len);
    hexdump(desc_buf, desc_len);
    desc.parse(desc_buf, desc_len);
    dump_fields();
    g_have_desc = true;
  } else {
    if (!tuh_descriptor_get_hid_report(daddr, info.desc.bInterfaceNumber, HID_DESC_TYPE_REPORT, 0,
                                       desc_buf, sizeof desc_buf, on_report_desc, 0)) {
      Serial.println("tuh_descriptor_get_hid_report() refused");
    }
  }
  // interrupt-IN polling disabled for the noise test
}

void tuh_hid_umount_cb(uint8_t daddr, uint8_t idx) {
  Serial.printf("HID itf unmounted: daddr %u idx %u\r\n", daddr, idx);
}

void tuh_hid_report_received_cb(uint8_t daddr, uint8_t idx, uint8_t const* report, uint16_t len) {
  Serial.printf("[%lu] INPUT report (%u bytes): ", millis(), len);
  hexdump(report, len);
  if (g_have_desc && len > 0) {
    char path[160];
    uint8_t rid = report[0];
    for (int i = 0; i < desc.size(); ++i) {
      const hid::Field& f = desc[i];
      if (f.type != hid::R_INPUT || f.report_id != rid) continue;
      int32_t raw = hid::Descriptor::raw_value(f, report + 1, len - 1);
      desc.format_path(f, path, sizeof path);
      Serial.printf("    %s = %ld (%.3f)\r\n", path, (long)raw, hid::Descriptor::physical_value(f, raw));
    }
  }
  (void)daddr; (void)idx;  // not re-armed: noise test
}

void tuh_hid_get_report_complete_cb(uint8_t daddr, uint8_t idx, uint8_t report_id, uint8_t report_type, uint16_t len) {
  (void)daddr; (void)idx; (void)report_type;
  g_busy = false;
  Serial.printf("[%lu] FEATURE report %u (%u bytes): ", millis(), report_id, len);
  if (len == 0) { Serial.println("(failed / stalled)"); return; }
  hexdump(report_buf, len);
  char path[160];
  // Device echoes the report ID as byte 0 when report IDs are in use.
  const uint8_t* payload = report_buf + 1;
  size_t plen = len ? len - 1 : 0;
  for (int i = 0; i < desc.size(); ++i) {
    const hid::Field& f = desc[i];
    if (f.type != hid::R_FEATURE || f.report_id != report_id) continue;
    int32_t raw = hid::Descriptor::raw_value(f, payload, plen);
    desc.format_path(f, path, sizeof path);
    double phys = hid::Descriptor::physical_value(f, raw);
    Serial.printf("    %-60s = %ld", path, (long)raw);
    if (phys != (double)raw) Serial.printf("  -> %.3f", phys);
    Serial.println();
  }
}

}  // extern "C"

static uint8_t max3421_revision_cs(int cs) {
  pinMode(cs, OUTPUT);
  digitalWrite(cs, HIGH);
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  // Switch to full-duplex SPI (PINCTL.FDUPSPI) then read REVISION (reg 18).
  digitalWrite(cs, LOW);
  SPI.transfer((17 << 3) | 0x02);
  SPI.transfer(0x10);
  digitalWrite(cs, HIGH);
  delayMicroseconds(5);
  digitalWrite(cs, LOW);
  SPI.transfer(18 << 3);
  uint8_t rev = SPI.transfer(0);
  digitalWrite(cs, HIGH);
  SPI.endTransaction();
  return rev;
}

// Which GPIO is the module's CS on? Try every M5-Bus GPIO that is not used by
// the LCD/SD/UART0/I2C and see which one returns a sane REVISION (0x12/0x13).
static int scan_cs() {
  SPI.begin(18, 19, 23, -1);
  const int cands[] = {5, 13, 0, 2, 12, 15, 16, 17, 25, 26};
  int found = -1;
  for (int cs : cands) {
    uint8_t r1 = max3421_revision_cs(cs);
    uint8_t r2 = max3421_revision_cs(cs);
    Serial.printf("  CS GPIO %2d -> REVISION 0x%02X 0x%02X%s\r\n", cs, r1, r2,
                  (r1 == r2 && (r1 == 0x12 || r1 == 0x13)) ? "  <== MAX3421E" : "");
    if (found < 0 && r1 == r2 && (r1 == 0x12 || r1 == 0x13)) found = cs;
  }
  return found;
}


// Bit-banged MAX3421E register read on plain GPIOs, run before M5.begin() so
// no SPI driver state can get in the way: tells hardware trouble from driver
// trouble.
static uint8_t bb_xfer(uint8_t out) {
  uint8_t in = 0;
  for (int b = 7; b >= 0; --b) {
    digitalWrite(23, (out >> b) & 1);
    delayMicroseconds(2);
    digitalWrite(18, HIGH);
    delayMicroseconds(2);
    in = (in << 1) | digitalRead(19);
    digitalWrite(18, LOW);
    delayMicroseconds(2);
  }
  return in;
}
static int bitbang_scan() {
  int found = -1;
  pinMode(18, OUTPUT); digitalWrite(18, LOW);
  pinMode(23, OUTPUT); digitalWrite(23, LOW);
  pinMode(19, INPUT);
  Serial.printf("MISO idle (no CS): %d\r\n", digitalRead(19));
  const int cands[] = {13, 5, 0};
  for (int cs : cands) {
    pinMode(cs, OUTPUT); digitalWrite(cs, HIGH); delayMicroseconds(10);
    digitalWrite(cs, LOW); bb_xfer((17 << 3) | 0x02); bb_xfer(0x10); digitalWrite(cs, HIGH);
    delayMicroseconds(10);
    digitalWrite(cs, LOW); uint8_t st = bb_xfer(18 << 3); uint8_t rev = bb_xfer(0); digitalWrite(cs, HIGH);
    delayMicroseconds(10);
    digitalWrite(cs, LOW); bb_xfer(17 << 3); uint8_t pinctl = bb_xfer(0); digitalWrite(cs, HIGH);
    Serial.printf("  bit-bang CS G%-2d: status 0x%02X REVISION 0x%02X PINCTL 0x%02X\r\n", cs, st, rev, pinctl);
    if (found < 0 && (rev == 0x12 || rev == 0x13)) found = cs;
    // Leave every candidate driven HIGH: a floating SS lets the MAX3421E
    // listen to the LCD's SPI traffic and drive MISO back.
  }
  pinMode(18, INPUT); pinMode(23, INPUT);
  return found;
}
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\r\nUPS Watcher probe " UPSW_VERSION);
  Serial.println("Bit-bang scan before any driver touches the bus:");
  int bb_cs = bitbang_scan();
  Serial.printf("bit-bang found CS = %d; G35=%d G34=%d\r\n", bb_cs, digitalRead(35), digitalRead(34));
  auto cfg = M5.config();
  cfg.internal_spk = false;  // Core Basic DAC speaker amp hums if GPIO 25 floats
  cfg.internal_mic = false;
  M5.begin(cfg);
  M5.Speaker.end();
  pinMode(25, OUTPUT);
  digitalWrite(25, LOW);
  M5.Display.setRotation(1);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(4, 4);
  M5.Display.println("UPS Watcher probe");
  Serial.println("Scanning for the MAX3421E chip select:");
  int cs = scan_cs();
  if (cs < 0) cs = bb_cs;
  Serial.printf("MAX3421E CS = GPIO %d\r\n", cs);
  Serial.printf("INT candidates idle levels: G35=%d G34=%d G36=%d G2=%d G13=%d\r\n",
                digitalRead(35), digitalRead(34), digitalRead(36), digitalRead(2), digitalRead(13));
  uint8_t rev = cs >= 0 ? max3421_revision_cs(cs) : 0xFF;
  Serial.printf("Power: type %d, battery %ld%%, charging %d\r\n", (int)M5.Power.getType(),
                (long)M5.Power.getBatteryLevel(), (int)M5.Power.isCharging());
  char l1[40];
  snprintf(l1, sizeof l1, "MAX3421E rev 0x%02X", rev);
  {
    LcdLock lk;
    M5.Display.setCursor(4, 30);
    M5.Display.println(l1);
  }
  if (cs < 0) { lcd_status("No MAX3421E found", "check DIP switches"); return; }
  pUSBHost = new M5_USBH_Host(&SPI, 18, 23, 19, cs, USBW_INT_PIN);
  Serial.printf("Starting USB host with CS %d INT %d\r\n", cs, USBW_INT_PIN);
  if (!USBHost.begin(0)) {
    Serial.println("USBHost.begin failed");
    lcd_status("USB host init failed");
    return;
  }
  Serial.println("USB host started, waiting for a device...");
  lcd_status("Waiting for UPS...");
}

void loop() {
  if (!pUSBHost) { delay(100); return; }
  USBHost.task();
  M5.update();

  if (g_mounted && g_have_desc && !g_strings_done) {
    g_strings_done = true;
    uint8_t sbuf[128];
    uint16_t langid = 0x0409;
    if (tuh_descriptor_get_manufacturer_string_sync(g_daddr, langid, sbuf, sizeof sbuf) == XFER_RESULT_SUCCESS)
      utf16_to_ascii(sbuf, sizeof sbuf, mfr, sizeof mfr);
    if (tuh_descriptor_get_product_string_sync(g_daddr, langid, sbuf, sizeof sbuf) == XFER_RESULT_SUCCESS)
      utf16_to_ascii(sbuf, sizeof sbuf, prod, sizeof prod);
    if (tuh_descriptor_get_serial_string_sync(g_daddr, langid, sbuf, sizeof sbuf) == XFER_RESULT_SUCCESS)
      utf16_to_ascii(sbuf, sizeof sbuf, serial_s, sizeof serial_s);
    Serial.printf("Manufacturer: '%s'  Product: '%s'  Serial: '%s'\r\n", mfr, prod, serial_s);
    lcd_status(prod, mfr);
    // Fetch the string indexes the power class points at, too.
    for (int i = 0; i < desc.size(); ++i) {
      const hid::Field& f = desc[i];
      uint16_t page = f.usage >> 16, id = f.usage & 0xFFFF;
      bool is_string = (page == 0x84 && (id == 0xFD || id == 0xFE || id == 0xFF || id == 0x01)) ||
                       (page == 0x85 && (id == 0x87 || id == 0x88 || id == 0x89 || id == 0x8F));
      if (!is_string || f.type != hid::R_FEATURE) continue;
      uint8_t rb[8] = {0};
      if (tuh_hid_get_report(g_daddr, g_idx, f.report_id, HID_REPORT_TYPE_FEATURE, rb, (desc.report_bits(f.report_id, hid::R_FEATURE) + 7) / 8 + 1)) {
        // completes through the callback; we only want the index so wait briefly
        uint32_t t0 = millis();
        g_busy = true;
        while (g_busy && millis() - t0 < 500) USBHost.task();
        // the callback printed into report_buf? no: rb. Re-read the raw byte.
        int32_t sidx = hid::Descriptor::raw_value(f, rb + 1, sizeof rb - 1);
        char path[120];
        desc.format_path(f, path, sizeof path);
        if (sidx > 0 && sidx < 256 &&
            tuh_descriptor_get_string_sync(g_daddr, (uint8_t)sidx, langid, sbuf, sizeof sbuf) == XFER_RESULT_SUCCESS) {
          char s[64];
          utf16_to_ascii(sbuf, sizeof sbuf, s, sizeof s);
          Serial.printf("String %s (index %ld) = '%s'\r\n", path, (long)sidx, s);
        } else {
          Serial.printf("String %s index %ld: not fetched\r\n", path, (long)sidx);
        }
      }
    }
  }

  if (g_mounted && g_have_desc && n_feats && !g_busy && millis() >= next_poll) {
    const hid::ReportInfo& r = feats[feat_i];
    uint16_t len = r.bytes() + 1;
    if (len > sizeof report_buf) len = sizeof report_buf;
    memset(report_buf, 0, sizeof report_buf);
    if (tuh_hid_get_report(g_daddr, g_idx, r.report_id, HID_REPORT_TYPE_FEATURE, report_buf, len)) {
      g_busy = true;
    } else {
      Serial.printf("get_report(%u) refused\r\n", r.report_id);
    }
    feat_i = (feat_i + 1) % n_feats;
    next_poll = millis() + (feat_i == 0 ? 10000 : 150);
    if (feat_i == 0) Serial.println("---- pass complete; next pass in 10 s ----");
  }
}
