// The variable table a NUT client sees: name -> value, plus the metadata
// upsd reports for GET TYPE / GET DESC. Plain C++17 so it builds natively.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace ups {

static constexpr int MAX_VARS = 80;
static constexpr int NAME_LEN = 40;
static constexpr int VALUE_LEN = 64;

enum VarFlags : uint8_t {
  F_STRING = 1,  // TYPE ... STRING:n, otherwise NUMBER
  F_RW = 2,      // appears in LIST RW, accepts SET
};

struct Var {
  char name[NAME_LEN];
  char value[VALUE_LEN];
  uint8_t flags;
  bool used;
};

class State {
 public:
  State();

  // Sets or replaces a variable. Returns false if the table is full.
  bool set(const char* name, const char* value, uint8_t flags = 0);
  bool set_num(const char* name, double value, int decimals, uint8_t flags = 0);
  bool set_int(const char* name, long value, uint8_t flags = 0);
  // Removes a variable (e.g. ups.alarm when there is no alarm).
  void remove(const char* name);
  const char* get(const char* name) const;
  bool has(const char* name) const { return get(name) != nullptr; }
  const Var* find(const char* name) const;

  int size() const;
  // Iterate in insertion (table) order; returns nullptr past the end.
  const Var* at(int index) const;

  // Driver has fresh data (dstate_dataok / dstate_datastale in NUT terms).
  bool stale() const { return stale_; }
  void set_stale(bool s) { stale_ = s; }

  // Instant commands the UPS currently supports (LIST CMD).
  bool add_cmd(const char* name);
  void clear_cmds();
  int cmd_count() const { return n_cmds_; }
  const char* cmd(int i) const { return i < n_cmds_ ? cmds_[i] : nullptr; }
  bool has_cmd(const char* name) const;

  void clear();

  // Monotonic counter bumped by every set() so the UI can cheaply detect change.
  uint32_t generation() const { return gen_; }

 private:
  Var vars_[MAX_VARS];
  char cmds_[16][32];
  int n_cmds_ = 0;
  bool stale_ = true;
  uint32_t gen_ = 0;
};

// Static descriptions for GET DESC, mirroring NUT's cmdvartab.
const char* describe_var(const char* name);
const char* describe_cmd(const char* name);

}  // namespace ups
