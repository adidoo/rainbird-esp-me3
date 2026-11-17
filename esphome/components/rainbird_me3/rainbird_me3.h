#pragma once

#include <deque>
#include <string>
#include <vector>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"

namespace esphome {
namespace rainbird_me3 {

// Master of the serial link with an ESP-ME3, in place of the LNK2.
// Reads (periodic polling) are always allowed; commands that act on
// irrigation are only sent once set_commands_allowed(true) has been called.
class RainbirdME3 : public PollingComponent, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_model_text(text_sensor::TextSensor *s) { model_ = s; }
  void set_controller_time_text(text_sensor::TextSensor *s) { time_ = s; }
  void set_controller_date_text(text_sensor::TextSensor *s) { date_ = s; }
  void set_active_zones_text(text_sensor::TextSensor *s) { active_zones_ = s; }
  void set_last_frame_text(text_sensor::TextSensor *s) { last_frame_ = s; }
  void set_active_zone_sensor(sensor::Sensor *s) { active_zone_ = s; }
  void set_rain_delay_sensor(sensor::Sensor *s) { rain_delay_ = s; }
  void set_irrigation_state_sensor(sensor::Sensor *s) { irrigation_state_ = s; }
  void set_rain_sensor_binary(binary_sensor::BinarySensor *s) { rain_sensor_ = s; }
  void set_link_binary(binary_sensor::BinarySensor *s) { link_ = s; }

  void set_commands_allowed(bool allowed) { commands_allowed_ = allowed; }
  bool commands_allowed() const { return commands_allowed_; }
  // Listen only: sends nothing, logs the frames seen on the line (e.g. LNK2 in parallel)
  void set_listen_only(bool on) { listen_only_ = on; }
  void set_gpio_pins(int tx, int rx) {
    tx_gpio_ = tx;
    rx_gpio_ = rx;
  }
  // Wake-up: at power-up the controller speaks Manchester 2400; handshake, then 04 04, 0D and 0E 02
  // (as the LNK2 does at start-up) switch the link to UART 9600.
  void wake();
  // Tests of the Manchester → UART switch (with « Écoute seule » on: nothing is sent automatically)
  void test_manchester(const std::string &hex);  // one raw Manchester frame, decoded reply in the log
  void test_manchester_handshake();              // 06 A → 86 T(A) B → 07 T(B) → 87, in Manchester
  void test_uart(const std::string &hex);        // one UART frame, reply in the log (« ← … » or nothing)

  // Commands (refused unless commands are allowed)
  bool run_zone(int zone, int minutes);   // 39 <zone16> <min>
  bool stop_irrigation();                 // 40
  bool run_program(int program);          // 38 <prog>, A = 0
  bool advance_zone();                    // 42 00
  bool stack_zone(int zone, int minutes);  // 4B <zone16> <min>: adds to the irrigation queue
  bool test_all_zones(int minutes);       // 3A <min>: all zones one after the other
  bool set_clock(int hour, int minute, int second, int day, int month, int year);  // 11 then 13

  // Current state
  bool session_ok() const { return session_ok_; }
  bool zone_active(int zone) const { return zone >= 1 && zone <= 32 && ((active_mask_ >> (zone - 1)) & 1); }
  bool irrigating() const { return active_mask_ != 0; }
  int zone_count() const { return nzones_; }

  // Configuration (object protocol). Read: cached values, read again when the session opens, after
  // each write and every 10 min. Write: refused without « Commandes autorisées ».
  void request_config();
  bool config_loaded() const { return config_loaded_; }
  int seasonal(int p) const { return ok_p_(p) ? cfg_.seasonal[p] : 0; }
  int frequency(int p) const { return ok_p_(p) ? cfg_.freq[p] : 0; }  // 0 chosen days, 2 odd, 3 even, 4 cyclic
  int interval(int p) const { return ok_p_(p) ? cfg_.interval[p] : 0; }
  int remaining_days(int p) const { return ok_p_(p) ? cfg_.remaining[p] : 0; }
  bool day(int p, int d) const { return ok_p_(p) && d >= 0 && d < 7 && cfg_.days[p][d]; }  // 0 = Sunday
  int start_minutes(int p, int i) const { return ok_p_(p) && i >= 0 && i < 6 ? cfg_.starts[p][i] : 1440; }
  std::string start_text(int p, int index) const;  // « HH:MM » or « --:-- »
  float duration_minutes(int p, int zone) const {
    return ok_p_(p) && zone >= 1 && zone <= 22 ? cfg_.dur[p][zone - 1] / 60.0f : 0;
  }
  int cycle(int zone) const { return zone >= 1 && zone <= 22 ? cfg_.cycle[zone - 1] : 0; }
  int soak(int zone) const { return zone >= 1 && zone <= 22 ? cfg_.soak[zone - 1] : 0; }
  bool rain_sensor_enabled() const { return cfg_.rain_sensor_enabled; }
  int rain_delay_days() const { return cfg_.rain_delay; }
  std::string program_summary(int p) const;
  // Compact data for the card: « s=30,735;f=0;d=1010101;i=3;r=1;p=115;z=3:1560,4:1560 »
  std::string program_data(int p) const;
  // Irrigation queue (3B 01 = running zone, 3B 02/03 = waiting): « 1:288;2:1200;5:2100 » (zone:seconds)
  std::string queue_text() const;
  int current_zone() const { return run_zone_; }
  int remaining_seconds() const { return run_left_; }

  bool set_seasonal(int p, int pct);
  bool set_frequency(int p, int mode) { return set_program_byte_(0x13, p, mode, "Fréquence"); }
  bool set_interval(int p, int days) { return set_program_byte_(0x12, p, days, "Intervalle"); }
  bool set_remaining_days(int p, int days) { return set_program_byte_(0x11, p, days, "Jours restants"); }
  bool set_day(int p, int day, bool on);
  bool set_start(int p, int index, int minutes);  // 1440 = no start
  bool set_start_text(int p, int index, const std::string &text);
  bool set_duration(int p, int zone, int seconds);
  bool set_cycle_soak(int zone, int cycle, int soak);
  bool set_rain_sensor_enabled(bool on);
  bool set_rain_delay(int days);
  // Writes a whole program; only the objects that change are sent. jours = 7 characters '0'/'1' (Sunday
  // first), departs = « HH:MM,HH:MM » (6 at most), durees = « zone:seconds,… » (missing zones = 0).
  bool write_program(int p, int freq, int interval, int remaining, const std::string &jours,
                     const std::string &departs, const std::string &durees, int pct);

  // Sends a raw hexadecimal payload (« 3F 00 »): reads always allowed, writes only when allowed
  bool send_hex(const std::string &hex, bool is_write);

 protected:
  struct Config {
    uint8_t rain_delay{0};
    bool rain_sensor_enabled{true};
    uint16_t seasonal[4]{};
    uint8_t freq[4]{}, interval[4]{}, remaining[4]{};
    bool days[4][7]{};
    uint16_t starts[4][6]{};
    uint32_t dur[4][22]{};
    uint8_t cycle[22]{}, soak[22]{};
  };
  static bool ok_p_(int p) { return p >= 0 && p <= 3; }
  void object_request_(const std::vector<uint8_t> &content, bool write = false);
  void handle_object_response_(const std::vector<uint8_t> &r);
  void store_(uint16_t obj, unsigned i0, unsigned i1, unsigned i2, uint32_t v);
  bool can_write_(const char *what);
  bool set_program_byte_(uint8_t obj, int p, int v, const char *what);

  Config cfg_;
  bool config_loaded_{false};
  bool config_requested_{false};
  bool reread_pending_{false};
  uint32_t active_mask_{0};
  int nzones_{13};
  uint32_t polls_{0};
  int run_zone_{0};
  int run_left_{0};
  std::vector<std::pair<uint8_t, uint32_t>> en_attente_[2];  // pages 2 and 3 of the queue
  void parse_queue_page_(const std::vector<uint8_t> &r);

  struct Request {
    std::vector<uint8_t> payload;
    bool write{false};
  };

  static uint8_t crc8_(const std::vector<uint8_t> &data);
  static std::string hex_(const std::vector<uint8_t> &data);
  void enqueue_(std::vector<uint8_t> payload, bool write = false);
  void transmit_(const std::vector<uint8_t> &payload);
  void feed_(uint8_t byte);
  void handle_frame_(const std::vector<uint8_t> &body);
  void handle_response_(const std::vector<uint8_t> &req, const std::vector<uint8_t> &resp);
  void set_link_(bool ok);
  void manchester_send_(const std::vector<uint8_t> &payload);
  void manchester_emit_(const std::vector<uint8_t> &payload);
  void capture_edges_(uint32_t ms, std::vector<std::pair<int64_t, int>> &edges);
  std::vector<uint8_t> decode_edges_(std::vector<std::pair<int64_t, int>> &edges);
  std::vector<uint8_t> manchester_xfer_(const std::vector<uint8_t> &payload, uint32_t ms);
  void manchester_begin_();
  void manchester_end_();
  static std::vector<uint8_t> parse_hex_(const std::string &hex);
  std::vector<uint8_t> manchester_listen_(uint32_t ms);  // returns the received payload (empty if none)
  // Session handshake (06/86 then 07/87): until it is done, the controller
  // ignores every command. Transform published by biggestchungus (HA thread 998452, post #18),
  // checked against our 355 handshakes captured on 03/11/2026.
  static uint32_t transform_(uint32_t x);
  void start_handshake_();

  std::deque<Request> queue_;
  Request current_;
  bool waiting_{false};
  uint32_t sent_at_{0};
  uint32_t next_send_at_{0};

  std::vector<uint8_t> rx_;
  bool in_frame_{false};
  bool escape_{false};

  uint32_t last_good_{0};
  uint32_t timeouts_{0};
  uint32_t crc_errors_{0};
  bool link_state_{false};
  bool link_published_{false};
  bool commands_allowed_{false};
  bool listen_only_{false};
  int tx_gpio_{18};
  int rx_gpio_{19};
  uint32_t misses_in_row_{0};
  uint32_t last_wake_{0};
  bool wake_pending_{false};
  bool model_known_{false};
  bool session_ok_{false};
  bool trace_fronts_{false};
  uint32_t challenge_{0};
  uint32_t next_handshake_at_{0};
  uint32_t handshakes_{0};

  text_sensor::TextSensor *model_{nullptr};
  text_sensor::TextSensor *time_{nullptr};
  text_sensor::TextSensor *date_{nullptr};
  text_sensor::TextSensor *active_zones_{nullptr};
  text_sensor::TextSensor *last_frame_{nullptr};
  sensor::Sensor *active_zone_{nullptr};
  sensor::Sensor *rain_delay_{nullptr};
  sensor::Sensor *irrigation_state_{nullptr};
  binary_sensor::BinarySensor *rain_sensor_{nullptr};
  binary_sensor::BinarySensor *link_{nullptr};
};

}  // namespace rainbird_me3
}  // namespace esphome
