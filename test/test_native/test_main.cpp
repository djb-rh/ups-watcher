// Host-side tests: pio test -e native
#include <unity.h>
#include <string>
#include <vector>
#include <cstring>
#include "hid_parser.h"
#include "ups_state.h"
#include "nut_protocol.h"

void setUp() {}
void tearDown() {}

// A small Power-Device style descriptor: UPS collection with a PowerSummary
// containing a PresentStatus bitfield (report 1, input+feature), a
// RemainingCapacity (report 2) and an Input collection with Voltage (report 3)
// using unit 0x00F0D121 exponent 7, and a signed 16-bit field (report 4).
static const uint8_t kDesc[] = {
  0x05, 0x84,        // Usage Page (Power Device)
  0x09, 0x04,        // Usage (UPS)
  0xA1, 0x01,        // Collection (Application)
  0x09, 0x24,        //   Usage (PowerSummary)
  0xA1, 0x02,        //   Collection (Logical)
  0x85, 0x01,        //     Report ID 1
  0x09, 0x02,        //     Usage (PresentStatus)
  0xA1, 0x02,        //     Collection (Logical)
  0x05, 0x85,        //       Usage Page (Battery System)
  0x09, 0xD0,        //       Usage (ACPresent)
  0x09, 0x44,        //       Usage (Charging)
  0x09, 0x45,        //       Usage (Discharging)
  0x09, 0x42,        //       Usage (BelowRemainingCapacityLimit)
  0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x04,
  0x81, 0x02,        //       Input (Var)
  0x09, 0xD0, 0x09, 0x44, 0x09, 0x45, 0x09, 0x42,
  0xB1, 0x02,        //       Feature (Var)
  0x95, 0x04, 0x81, 0x01, 0xB1, 0x01,  // 4 bits padding, both
  0xC0,              //     End Collection
  0x85, 0x02,        //     Report ID 2
  0x09, 0x66,        //     Usage (RemainingCapacity)
  0x15, 0x00, 0x26, 0x64, 0x00, 0x75, 0x08, 0x95, 0x01,
  0xB1, 0x82,        //     Feature (Var, Volatile)
  0xC0,              //   End Collection
  0x05, 0x84,        //   Usage Page (Power Device)
  0x09, 0x1A,        //   Usage (Input)
  0xA1, 0x02,        //   Collection
  0x85, 0x03,        //     Report ID 3
  0x09, 0x30,        //     Usage (Voltage)
  0x67, 0x21, 0xD1, 0xF0, 0x00,  // Unit (Volt)
  0x55, 0x07,        //     Unit Exponent 7
  0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x01,
  0xB1, 0x02,        //     Feature
  0x85, 0x04,        //     Report ID 4
  0x09, 0x31,        //     Usage (Current)
  0x66, 0x01, 0x00,  //     Unit (Ampere)
  0x55, 0x0E,        //     Unit Exponent -2 (nibble encoding)
  0x16, 0x00, 0x80, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x01,
  0xB1, 0x02,        //     Feature
  0xC0,              //   End Collection
  0xC0,              // End Collection
};

static void test_parse_fields() {
  hid::Descriptor d;
  TEST_ASSERT_TRUE(d.parse(kDesc, sizeof kDesc));
  // 4 input status bits + 4 feature status bits + capacity + voltage + current
  TEST_ASSERT_EQUAL(11, d.size());
  uint32_t path[] = {0x00840004, 0x00840024, 0x00840002};
  const hid::Field* ac = d.find(0x008500D0, path, 3, hid::R_FEATURE);
  TEST_ASSERT_NOT_NULL(ac);
  TEST_ASSERT_EQUAL(1, ac->report_id);
  TEST_ASSERT_EQUAL(0, ac->bit_offset);
  const hid::Field* dis = d.find(0x00850045, path, 3, hid::R_INPUT);
  TEST_ASSERT_NOT_NULL(dis);
  TEST_ASSERT_EQUAL(2, dis->bit_offset);
  TEST_ASSERT_EQUAL(8, d.report_bits(1, hid::R_FEATURE));
  TEST_ASSERT_EQUAL(8, d.report_bits(1, hid::R_INPUT));
  char p[128];
  d.format_path(*ac, p, sizeof p);
  TEST_ASSERT_EQUAL_STRING("UPS.PowerSummary.PresentStatus.ACPresent", p);
}

static void test_values_and_units() {
  hid::Descriptor d;
  d.parse(kDesc, sizeof kDesc);
  uint32_t inpath[] = {0x0084001A};
  const hid::Field* v = d.find(0x00840030, inpath, 1);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_EQUAL(3, v->report_id);
  uint8_t rep[1] = {120};
  TEST_ASSERT_EQUAL(120, hid::Descriptor::raw_value(*v, rep, 1));
  TEST_ASSERT_EQUAL_DOUBLE(120.0, hid::Descriptor::physical_value(*v, 120));
  const hid::Field* c = d.find(0x00840031, inpath, 1);
  TEST_ASSERT_NOT_NULL(c);
  TEST_ASSERT_EQUAL(-2, c->unit_exp);
  TEST_ASSERT_TRUE(c->is_signed());
  uint8_t rep2[2] = {0xF6, 0xFF};  // -10
  TEST_ASSERT_EQUAL(-10, hid::Descriptor::raw_value(*c, rep2, 2));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, -0.1, hid::Descriptor::physical_value(*c, -10));
  // status bits
  uint32_t path[] = {0x00840002};
  const hid::Field* dis = d.find(0x00850045, path, 1, hid::R_FEATURE);
  uint8_t st[1] = {0x04};
  TEST_ASSERT_EQUAL(1, hid::Descriptor::raw_value(*dis, st, 1));
  const hid::Field* ac = d.find(0x008500D0, path, 1, hid::R_FEATURE);
  TEST_ASSERT_EQUAL(0, hid::Descriptor::raw_value(*ac, st, 1));
}

static void test_state() {
  ups::State s;
  TEST_ASSERT_TRUE(s.set("ups.status", "OL", ups::F_STRING));
  TEST_ASSERT_TRUE(s.set_num("battery.charge", 99.6, 0));
  TEST_ASSERT_EQUAL_STRING("100", s.get("battery.charge"));
  TEST_ASSERT_EQUAL(2, s.size());
  s.set("ups.status", "OB LB", ups::F_STRING);
  TEST_ASSERT_EQUAL(2, s.size());
  TEST_ASSERT_EQUAL_STRING("OB LB", s.get("ups.status"));
  s.remove("ups.status");
  TEST_ASSERT_NULL(s.get("ups.status"));
  TEST_ASSERT_EQUAL(1, s.size());
}

static std::string run(nut::Session& ses, const nut::Config& cfg, const ups::State& st, const char* line, bool* keep = nullptr) {
  std::string out;
  nut::Hooks hooks;
  hooks.instcmd = [](const char* c, const char*) { return strcmp(c, "beeper.mute") == 0; };
  std::vector<std::string> clients = {"10.0.0.5"};
  bool k = ses.handle_line(line, cfg, st, hooks, out, 1, &clients);
  if (keep) *keep = k;
  return out;
}

static void test_protocol_basic() {
  ups::State st;
  st.set("ups.status", "OL CHRG", ups::F_STRING);
  st.set("battery.charge", "87");
  st.set("ups.model", "Back-UPS \"ES\" 550", ups::F_STRING);
  st.set("battery.charge.low", "10", ups::F_RW);
  st.add_cmd("beeper.mute");
  st.set_stale(false);
  nut::Config cfg; cfg.ups_name = "ups"; cfg.ups_desc = "Rack UPS"; cfg.version = "1.0";
  nut::Session s; s.reset("10.0.0.5");
  TEST_ASSERT_EQUAL_STRING("BEGIN LIST UPS\nUPS ups \"Rack UPS\"\nEND LIST UPS\n", run(s, cfg, st, "LIST UPS").c_str());
  TEST_ASSERT_EQUAL_STRING("VAR ups battery.charge \"87\"\n", run(s, cfg, st, "GET VAR ups battery.charge").c_str());
  TEST_ASSERT_EQUAL_STRING("VAR ups ups.model \"Back-UPS \\\"ES\\\" 550\"\n", run(s, cfg, st, "GET VAR ups ups.model").c_str());
  TEST_ASSERT_EQUAL_STRING("ERR VAR-NOT-SUPPORTED\n", run(s, cfg, st, "GET VAR ups nope").c_str());
  TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-UPS\n", run(s, cfg, st, "GET VAR other battery.charge").c_str());
  TEST_ASSERT_EQUAL_STRING("TYPE ups battery.charge.low RW NUMBER\n", run(s, cfg, st, "GET TYPE ups battery.charge.low").c_str());
  TEST_ASSERT_EQUAL_STRING("TYPE ups ups.status STRING:63\n", run(s, cfg, st, "GET TYPE ups ups.status").c_str());
  std::string lv = run(s, cfg, st, "LIST VAR ups");
  TEST_ASSERT_TRUE(lv.rfind("BEGIN LIST VAR ups\n", 0) == 0);
  TEST_ASSERT_TRUE(lv.find("VAR ups ups.status \"OL CHRG\"\n") != std::string::npos);
  TEST_ASSERT_TRUE(lv.find("END LIST VAR ups\n") != std::string::npos);
  TEST_ASSERT_EQUAL_STRING("BEGIN LIST RW ups\nRW ups battery.charge.low \"10\"\nEND LIST RW ups\n", run(s, cfg, st, "LIST RW ups").c_str());
  TEST_ASSERT_EQUAL_STRING("BEGIN LIST CMD ups\nCMD ups beeper.mute\nEND LIST CMD ups\n", run(s, cfg, st, "LIST CMD ups").c_str());
  TEST_ASSERT_EQUAL_STRING("BEGIN LIST CLIENT ups\nCLIENT ups 10.0.0.5\nEND LIST CLIENT ups\n", run(s, cfg, st, "LIST CLIENT ups").c_str());
  TEST_ASSERT_EQUAL_STRING("NUMLOGINS ups 1\n", run(s, cfg, st, "GET NUMLOGINS ups").c_str());
  TEST_ASSERT_EQUAL_STRING("1.3\n", run(s, cfg, st, "NETVER").c_str());
  TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-COMMAND\n", run(s, cfg, st, "FROB").c_str());
  TEST_ASSERT_EQUAL_STRING("OK\n", run(s, cfg, st, "LOGIN ups").c_str());
  TEST_ASSERT_EQUAL_STRING("ERR ALREADY-LOGGED-IN\n", run(s, cfg, st, "LOGIN ups").c_str());
  TEST_ASSERT_EQUAL_STRING("OK\n", run(s, cfg, st, "INSTCMD ups beeper.mute").c_str());
  TEST_ASSERT_EQUAL_STRING("ERR CMD-NOT-SUPPORTED\n", run(s, cfg, st, "INSTCMD ups load.off").c_str());
  bool keep = true;
  TEST_ASSERT_EQUAL_STRING("OK Goodbye\n", run(s, cfg, st, "LOGOUT", &keep).c_str());
  TEST_ASSERT_FALSE(keep);
}

static void test_protocol_auth_and_stale() {
  ups::State st;
  st.set("ups.status", "OL", ups::F_STRING);
  nut::Config cfg; cfg.username = "monuser"; cfg.password = "secret";
  nut::Session s; s.reset("x");
  TEST_ASSERT_EQUAL_STRING("ERR DATA-STALE\n", run(s, cfg, st, "GET VAR ups ups.status").c_str());
  st.set_stale(false);
  TEST_ASSERT_EQUAL_STRING("ERR USERNAME-REQUIRED\n", run(s, cfg, st, "LOGIN ups").c_str());
  TEST_ASSERT_EQUAL_STRING("OK\n", run(s, cfg, st, "USERNAME monuser").c_str());
  TEST_ASSERT_EQUAL_STRING("ERR PASSWORD-REQUIRED\n", run(s, cfg, st, "LOGIN ups").c_str());
  TEST_ASSERT_EQUAL_STRING("OK\n", run(s, cfg, st, "PASSWORD wrong").c_str());
  TEST_ASSERT_EQUAL_STRING("ERR ACCESS-DENIED\n", run(s, cfg, st, "LOGIN ups").c_str());
  TEST_ASSERT_EQUAL_STRING("ERR ALREADY-SET-PASSWORD\n", run(s, cfg, st, "PASSWORD secret").c_str());
  // GET/LIST never need auth
  TEST_ASSERT_EQUAL_STRING("VAR ups ups.status \"OL\"\n", run(s, cfg, st, "GET VAR ups ups.status").c_str());
  nut::Session s2; s2.reset("y");
  run(s2, cfg, st, "USERNAME monuser"); run(s2, cfg, st, "PASSWORD secret");
  TEST_ASSERT_EQUAL_STRING("OK\n", run(s2, cfg, st, "LOGIN ups").c_str());
  TEST_ASSERT_EQUAL_STRING("OK PRIMARY-GRANTED\n", run(s2, cfg, st, "PRIMARY ups").c_str());
}

static void test_tokenize() {
  std::string t[6];
  int n = nut::tokenize("SET VAR ups battery.charge.low \"a \\\"quoted\\\" value\" # comment", t, 6);
  TEST_ASSERT_EQUAL(5, n);
  TEST_ASSERT_EQUAL_STRING("a \"quoted\" value", t[4].c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_fields);
  RUN_TEST(test_values_and_units);
  RUN_TEST(test_state);
  RUN_TEST(test_tokenize);
  RUN_TEST(test_protocol_basic);
  RUN_TEST(test_protocol_auth_and_stale);
  return UNITY_END();
}
