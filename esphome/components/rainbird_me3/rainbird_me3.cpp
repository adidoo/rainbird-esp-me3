#include "rainbird_me3.h"

#include <algorithm>
#include <cstdio>

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_rom_gpio.h>
#include <esp_timer.h>
#include <soc/gpio_sig_map.h>

#include "esphome/components/uart/uart_component_esp_idf.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace rainbird_me3 {

static const char *const TAG = "rainbird_me3";

static const uint8_t FRAME_START = 0x1C;
static const uint8_t FRAME_END = 0x1D;
static const uint8_t FRAME_ESC = 0x1B;
static const uint32_t RESPONSE_TIMEOUT_MS = 500;  // the controller answers within 6 to 40 ms
static const uint32_t GAP_MS = 100;                // spacing between requests, as the LNK2 does
static const uint32_t LINK_LOST_MS = 60000;
static const size_t MAX_QUEUE = 60;
static const size_t MAX_FRAME = 300;
static const uint32_t MISSES_BEFORE_WAKE = 3;     // unanswered requests in a row before a wake-up
static const uint32_t WAKE_INTERVAL_MS = 30000;   // at most one wake-up every 30 s
static const int64_t HALF_BIT_NS = 208333;        // Manchester 2400 baud
static const uint32_t HANDSHAKE_RETRY_MS = 2000;  // delay between two handshake attempts
static const uint32_t HANDSHAKE_MASK = 0x80718611;
static const int HANDSHAKE_STEPS = 13;

uint32_t RainbirdME3::transform_(uint32_t x) {
  // 13 steps of a 32-bit shift register: parity of (x & mask) shifted in on the right.
  for (int i = 0; i < HANDSHAKE_STEPS; i++) {
    uint32_t fb = __builtin_parity(x & HANDSHAKE_MASK);
    x = (x << 1) | fb;
    if (x == 0)
      x = 0xFFFFFFFF;
  }
  return x;
}

void RainbirdME3::start_handshake_() {
  this->challenge_ = random_uint32();
  this->handshakes_++;
  uint32_t a = this->challenge_;
  ESP_LOGI(TAG, "Poignée de main n° %u : défi %08X", (unsigned) this->handshakes_, (unsigned) a);
  this->current_ = Request{{0x06, (uint8_t) (a >> 24), (uint8_t) (a >> 16), (uint8_t) (a >> 8), (uint8_t) a}, false};
  this->transmit_(this->current_.payload);
  this->waiting_ = true;
  this->sent_at_ = millis();
}

uint8_t RainbirdME3::crc8_(const std::vector<uint8_t> &data) {
  uint8_t c = 0;
  for (uint8_t b : data) {
    c ^= b;
    for (int i = 0; i < 8; i++)
      c = (c & 0x80) ? (uint8_t) ((c << 1) ^ 0x07) : (uint8_t) (c << 1);
  }
  return c;
}

std::string RainbirdME3::hex_(const std::vector<uint8_t> &data) {
  std::string out;
  char buf[4];
  for (size_t i = 0; i < data.size(); i++) {
    snprintf(buf, sizeof(buf), i ? " %02X" : "%02X", data[i]);
    out += buf;
  }
  return out;
}

void RainbirdME3::setup() {
  // Nothing to send here: loop() opens the session (handshake), then identifies the controller.
}

void RainbirdME3::dump_config() {
  ESP_LOGCONFIG(TAG, "Rain Bird ESP-ME3 (UART 9600 8N1, CRC-8 0x07)");
  LOG_UPDATE_INTERVAL(this);
  ESP_LOGCONFIG(TAG, "  Commandes autorisées : %s", YESNO(this->commands_allowed_));
}

void RainbirdME3::update() {
  // Polling cycle (reads only)
  if (this->listen_only_ || !this->session_ok_)
    return;
  if (!this->model_known_)
    this->enqueue_({0x02});
  this->enqueue_({0x10});        // time
  this->enqueue_({0x12});        // date
  this->enqueue_({0x3F, 0x00});  // active zones
  this->enqueue_({0x48});        // irrigation state (dial)
  this->enqueue_({0x36});        // rain delay
  this->enqueue_({0x3E});        // rain sensor
  this->enqueue_({0x3B, 0x00});  // irrigation queue
  if (this->active_mask_ != 0) {  // while watering: running zone + remaining time, then waiting zones
    this->enqueue_({0x3B, 0x01});
    this->enqueue_({0x3B, 0x02});
    this->enqueue_({0x3B, 0x03});
  }
  if (++this->polls_ % 40 == 0)  // every 10 min: read the configuration again (changed on the dial?)
    this->request_config();
}

void RainbirdME3::enqueue_(std::vector<uint8_t> payload, bool write) {
  if (this->queue_.size() >= MAX_QUEUE) {
    ESP_LOGW(TAG, "File pleine, requête %s abandonnée", hex_(payload).c_str());
    return;
  }
  this->queue_.push_back(Request{std::move(payload), write});
}

void RainbirdME3::transmit_(const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> body;
  body.push_back((uint8_t) payload.size());
  body.push_back(0x00);
  body.insert(body.end(), payload.begin(), payload.end());
  body.push_back(crc8_(body));

  std::vector<uint8_t> out;
  out.push_back(FRAME_START);
  for (uint8_t b : body) {
    if (b == FRAME_START) {
      out.push_back(FRAME_ESC);
      out.push_back(0x01);
    } else if (b == FRAME_ESC) {
      out.push_back(FRAME_ESC);
      out.push_back(0x02);
    } else if (b == FRAME_END) {
      out.push_back(FRAME_ESC);
      out.push_back(0x03);
    } else {
      out.push_back(b);
    }
  }
  out.push_back(FRAME_END);
  this->write_array(out);
  this->flush();
  ESP_LOGD(TAG, "→ %s", hex_(payload).c_str());
}

void RainbirdME3::loop() {
  while (this->available()) {
    uint8_t b;
    if (!this->read_byte(&b))
      break;
    this->feed_(b);
  }

  uint32_t now = millis();
  // After a series of writes: a single full read-back, once the queue is empty
  if (this->reread_pending_ && this->queue_.empty() && !this->waiting_ && this->session_ok_) {
    this->reread_pending_ = false;
    this->config_requested_ = false;
    this->request_config();
  }
  if (this->waiting_ && now - this->sent_at_ > RESPONSE_TIMEOUT_MS) {
    this->waiting_ = false;
    this->timeouts_++;
    this->next_send_at_ = now + GAP_MS;
    ESP_LOGW(TAG, "Pas de réponse à %s (%u sans réponse)", hex_(this->current_.payload).c_str(),
             (unsigned) this->timeouts_);
    uint8_t op = this->current_.payload.empty() ? 0 : this->current_.payload[0];
    if (op == 0x06 || op == 0x07) {
      this->next_handshake_at_ = now + HANDSHAKE_RETRY_MS;
      if (this->handshakes_ % 5 == 0)
        this->wake_pending_ = true;
    } else if (++this->misses_in_row_ >= MISSES_BEFORE_WAKE && this->session_ok_) {
      ESP_LOGW(TAG, "Session perdue : nouvelle poignée de main");
      this->session_ok_ = false;
      this->config_requested_ = false;
      this->queue_.clear();
    }
  }
  if (this->wake_pending_ && !this->waiting_ && !this->listen_only_) {
    this->wake_pending_ = false;
    this->wake();
    now = millis();
  }
  // Outside a session only the 07 answer may go out; the rest of the queue waits for the session.
  bool answer_ready = !this->queue_.empty() && !this->queue_.front().payload.empty() &&
                      this->queue_.front().payload[0] == 0x07;
  if (!this->listen_only_ && !this->waiting_ && !this->session_ok_ && !answer_ready &&
      (int32_t) (now - this->next_handshake_at_) >= 0 && (int32_t) (now - this->next_send_at_) >= 0) {
    this->start_handshake_();
    now = millis();
  }
  if (!this->listen_only_ && !this->waiting_ && !this->queue_.empty() && (this->session_ok_ || answer_ready) &&
      (int32_t) (now - this->next_send_at_) >= 0) {
    this->current_ = this->queue_.front();
    this->queue_.pop_front();
    this->transmit_(this->current_.payload);
    this->waiting_ = true;
    this->sent_at_ = now;
  }
  bool ok = this->last_good_ != 0 && now - this->last_good_ < LINK_LOST_MS;
  if (ok != this->link_state_ || !this->link_published_)
    this->set_link_(ok);
}

void RainbirdME3::manchester_emit_(const std::vector<uint8_t> &payload) {
  // Preamble F5 F5, then 1C <len> <data> <sum of len + data> 1D, most significant bit first.
  // Bit 1 = high then low half-bits, bit 0 = low then high; idle low (measured on the LNK2, 03/11/2026).
  std::vector<uint8_t> bytes = {0xF5, 0xF5, 0x1C, (uint8_t) payload.size()};
  uint8_t sum = payload.size();
  for (uint8_t b : payload) {
    bytes.push_back(b);
    sum += b;
  }
  bytes.push_back(sum);
  bytes.push_back(0x1D);
  std::vector<uint8_t> half;
  for (uint8_t b : bytes) {
    for (int i = 7; i >= 0; i--) {
      bool bit = (b >> i) & 1;
      half.push_back(bit ? 1 : 0);
      half.push_back(bit ? 0 : 1);
    }
  }
  gpio_num_t tx = (gpio_num_t) this->tx_gpio_;
  int64_t t0 = esp_timer_get_time() * 1000;
  for (size_t i = 0; i < half.size(); i++) {
    gpio_set_level(tx, half[i]);
    int64_t fin = t0 + (int64_t) (i + 1) * HALF_BIT_NS;
    while (esp_timer_get_time() * 1000 < fin) {
    }
  }
  gpio_set_level(tx, 0);
}

void RainbirdME3::capture_edges_(uint32_t ms, std::vector<std::pair<int64_t, int>> &edges) {
  gpio_num_t rx = (gpio_num_t) this->rx_gpio_;
  edges.clear();
  int64_t debut = esp_timer_get_time(), fin = debut + (int64_t) ms * 1000;
  int prec = gpio_get_level(rx);
  edges.push_back({debut, prec});
  int64_t t;
  while ((t = esp_timer_get_time()) < fin && edges.size() < 1500) {
    int lv = gpio_get_level(rx);
    if (lv != prec) {
      edges.push_back({t, lv});
      prec = lv;
    }
  }
}

std::vector<uint8_t> RainbirdME3::manchester_xfer_(const std::vector<uint8_t> &payload, uint32_t ms) {
  // Transmit + capture of the reply without interruption: WiFi made us lose edges (absurd durations)
  // and distorted the software-timed transmission. Under 300 ms (interrupt watchdog).
  static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  std::vector<std::pair<int64_t, int>> edges;
  edges.reserve(1500);
  if (ms > 160)
    ms = 160;
  portENTER_CRITICAL(&mux);
  this->manchester_emit_(payload);
  this->capture_edges_(ms, edges);
  portEXIT_CRITICAL(&mux);
  ESP_LOGD(TAG, "→ Manchester %s", hex_(payload).c_str());
  return this->decode_edges_(edges);
}

void RainbirdME3::manchester_send_(const std::vector<uint8_t> &payload) {
  this->manchester_emit_(payload);
  ESP_LOGD(TAG, "→ Manchester %s", hex_(payload).c_str());
}

std::vector<uint8_t> RainbirdME3::manchester_listen_(uint32_t ms) {
  std::vector<std::pair<int64_t, int>> edges;
  this->capture_edges_(ms, edges);
  return this->decode_edges_(edges);
}

std::vector<uint8_t> RainbirdME3::decode_edges_(std::vector<std::pair<int64_t, int>> &edges) {
  // On the controller side the convention is inverted: low then high = 1.
  // Glitch filter: a pulse shorter than 60 µs is merged into the surrounding level.
  for (size_t i = 1; i + 1 < edges.size();) {
    if (edges[i + 1].first - edges[i].first < 60) {
      edges.erase(edges.begin() + i, edges.begin() + i + 2);
      if (i > 1)
        i--;
    } else {
      i++;
    }
  }
  if (this->trace_fronts_) {  // tests: every edge, in chunks (first level, then durations in µs)
    std::string brut = "niveau initial " + to_string(edges.empty() ? -1 : edges[0].second) + " :";
    int morceau = 0;
    for (size_t i = 1; i < edges.size(); i++) {
      brut += " " + to_string((long long) (edges[i].first - edges[i - 1].first));
      if (brut.size() > 380 || i + 1 == edges.size()) {
        ESP_LOGI(TAG, "TRACE %d %s", morceau++, brut.c_str());
        brut.clear();
      }
    }
  }
  // The optocouplers stretch one level and shorten the other: the bias is measured on the
  // single half-bits (median of the short durations of each level) and removed before rounding.
  std::vector<int64_t> courts[2];
  for (size_t i = 1; i + 1 < edges.size(); i++) {
    int64_t d = edges[i + 1].first - edges[i].first;
    if (d < 312)
      courts[edges[i].second ? 1 : 0].push_back(d);
  }
  int64_t biais = 0;
  if (courts[0].size() >= 4 && courts[1].size() >= 4) {
    for (auto &c : courts)
      std::nth_element(c.begin(), c.begin() + c.size() / 2, c.end());
    biais = (courts[1][courts[1].size() / 2] - courts[0][courts[0].size() / 2]) / 2;
  }
  ESP_LOGD(TAG, "   %u fronts, biais haut/bas %lld µs", (unsigned) edges.size(), (long long) biais);
  std::string half;
  for (size_t i = 1; i + 1 < edges.size(); i++) {
    int64_t d = edges[i + 1].first - edges[i].first;
    if (d < 1500)
      d += edges[i].second ? -biais : biais;
    int n = (int) ((d * 1000 + HALF_BIT_NS / 2) / HALF_BIT_NS);
    if (n < 1 || n > 4)  // idle or glitch: only the frame body is kept
      continue;
    half.append(n, edges[i].second ? '1' : '0');
  }
  std::string bits;
  for (int off = 0; off < 2 && bits.find("1111010111110101") == std::string::npos; off++) {
    bits.clear();
    for (size_t i = off; i + 1 < half.size(); i += 2)
      bits += (half[i] == '0' && half[i + 1] == '1') ? '1' : (half[i] == '1' && half[i + 1] == '0') ? '0' : 'x';
  }
  size_t p = bits.find("1111010111110101");
  if (p == std::string::npos) {
    ESP_LOGW(TAG, "← Manchester : rien de lisible (%u fronts)", (unsigned) edges.size());
    std::string brut;
    for (size_t i = 1; i < edges.size() && brut.size() < 400; i++) {
      char buf[16];
      snprintf(buf, sizeof(buf), "%d:%lld ", edges[i - 1].second, (long long) (edges[i].first - edges[i - 1].first));
      brut += buf;
    }
    ESP_LOGD(TAG, "   fronts (niveau:µs) %s", brut.c_str());
    return {};
  }
  std::vector<uint8_t> out;
  for (size_t i = p + 16; i + 8 <= bits.size(); i += 8) {
    uint8_t v = 0;
    for (int k = 0; k < 8; k++)
      v = (v << 1) | (bits[i + k] == '1');
    out.push_back(v);
    if (v == 0x1D)
      break;
  }
  ESP_LOGI(TAG, "← Manchester %s", hex_(out).c_str());
  // out = [F5] 1C <len> <payload> <sum> 1D: return the payload
  size_t d = 0;
  while (d < out.size() && out[d] != 0x1C)
    d++;
  if (d + 2 >= out.size() || d + 2 + out[d + 1] >= out.size())
    return {};
  uint8_t somme = out[d + 1];
  for (size_t i = 0; i < out[d + 1]; i++)
    somme += out[d + 2 + i];
  if (somme != out[d + 2 + out[d + 1]]) {
    ESP_LOGW(TAG, "   somme de contrôle fausse (calculée %02X) : trame rejetée", somme);
    std::string brut;
    for (size_t i = 1; i < edges.size() && brut.size() < 900; i++) {
      char buf[16];
      snprintf(buf, sizeof(buf), "%d:%lld ", edges[i - 1].second, (long long) (edges[i].first - edges[i - 1].first));
      brut += buf;
    }
    ESP_LOGD(TAG, "   fronts (niveau:µs) %s", brut.c_str());
    return {};
  }
  return std::vector<uint8_t>(out.begin() + d + 2, out.begin() + d + 2 + out[d + 1]);
}

void RainbirdME3::wake() {
  auto *idf = static_cast<uart::IDFUARTComponent *>(this->parent_);
  uart_port_t port = (uart_port_t) idf->get_hw_serial_number();
  gpio_num_t tx = (gpio_num_t) this->tx_gpio_;
  ESP_LOGI(TAG, "Réveil du programmateur (Manchester 2400 → UART 9600)");
  this->last_wake_ = millis();
  this->misses_in_row_ = 0;
  // Take the transmit pin back from the UART and set it to the Manchester idle level (low)
  esp_rom_gpio_connect_out_signal(tx, SIG_GPIO_OUT_IDX, false, false);
  gpio_set_direction(tx, GPIO_MODE_OUTPUT);
  gpio_set_level(tx, 0);
  delay(10);
  // Manchester handshake (as on the ESP-TM2): 06 A → 86 T(A) B → 07 T(B) → 87
  uint32_t a = random_uint32();
  std::vector<uint8_t> r = this->manchester_xfer_({0x06, (uint8_t) (a >> 24), (uint8_t) (a >> 16), (uint8_t) (a >> 8), (uint8_t) a}, 150);
  if (r.size() >= 9 && r[0] == 0x86) {
    uint32_t ta = ((uint32_t) r[1] << 24) | (r[2] << 16) | (r[3] << 8) | r[4];
    uint32_t b = ((uint32_t) r[5] << 24) | (r[6] << 16) | (r[7] << 8) | r[8];
    uint32_t tb = transform_(b);
    ESP_LOGI(TAG, "Manchester : 86 reçu, T(A) %s", ta == transform_(a) ? "correct" : "FAUX");
    this->manchester_xfer_({0x07, (uint8_t) (tb >> 24), (uint8_t) (tb >> 16), (uint8_t) (tb >> 8), (uint8_t) tb}, 150);
  }
  this->manchester_xfer_({0x04, 0x04}, 100);
  this->manchester_xfer_({0x0D}, 100);
  this->manchester_send_({0x0E, 0x02});
  delay(7);
  // Give the pin back to the UART (idle high) and resume polling
  gpio_set_level(tx, 1);
  uart_set_pin(port, this->tx_gpio_, this->rx_gpio_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  delay(10);
  uart_flush_input(port);
  this->in_frame_ = false;
  this->session_ok_ = false;  // loop() restarts the handshake in UART
  this->queue_.clear();
}

void RainbirdME3::set_link_(bool ok) {
  this->link_state_ = ok;
  this->link_published_ = true;
  if (this->link_ != nullptr)
    this->link_->publish_state(ok);
}

void RainbirdME3::feed_(uint8_t b) {
  if (b == FRAME_START) {  // start of frame (never present unescaped inside a frame)
    this->rx_.clear();
    this->in_frame_ = true;
    this->escape_ = false;
    return;
  }
  if (!this->in_frame_) {
    ESP_LOGD(TAG, "Octet hors trame : %02X", b);
    return;
  }
  if (this->escape_) {
    this->escape_ = false;
    if (b == 0x01)
      this->rx_.push_back(FRAME_START);
    else if (b == 0x02)
      this->rx_.push_back(FRAME_ESC);
    else if (b == 0x03)
      this->rx_.push_back(FRAME_END);
    else {
      ESP_LOGW(TAG, "Échappement inconnu 1B %02X", b);
      this->in_frame_ = false;
    }
    return;
  }
  if (b == FRAME_ESC) {
    this->escape_ = true;
    return;
  }
  if (b == FRAME_END) {
    this->in_frame_ = false;
    this->handle_frame_(this->rx_);
    return;
  }
  if (this->rx_.size() < MAX_FRAME)
    this->rx_.push_back(b);
  else
    this->in_frame_ = false;
}

void RainbirdME3::handle_frame_(const std::vector<uint8_t> &body) {
  // body = len 00 payload crc
  if (body.size() < 4 || body[0] != body.size() - 3) {
    this->crc_errors_++;
    ESP_LOGW(TAG, "Trame de longueur incohérente : %s", hex_(body).c_str());
    return;
  }
  std::vector<uint8_t> covered(body.begin(), body.end() - 1);
  if (crc8_(covered) != body.back()) {
    this->crc_errors_++;
    ESP_LOGW(TAG, "CRC faux : %s", hex_(body).c_str());
    return;
  }
  std::vector<uint8_t> payload(body.begin() + 2, body.end() - 1);
  ESP_LOGD(TAG, "← %s", hex_(payload).c_str());
  if (this->last_frame_ != nullptr)
    this->last_frame_->publish_state(hex_(payload));
  this->last_good_ = millis();
  this->misses_in_row_ = 0;
  if (this->waiting_) {
    this->waiting_ = false;
    this->next_send_at_ = millis() + GAP_MS;
    this->handle_response_(this->current_.payload, payload);
  }
}

void RainbirdME3::handle_response_(const std::vector<uint8_t> &req, const std::vector<uint8_t> &r) {
  if (req.empty() || r.empty())
    return;
  uint8_t op = req[0];
  if (op == 0x06) {  // ← 86 T(A) B: the controller answers our challenge and sets its own
    if (r[0] != 0x86 || r.size() < 9) {
      ESP_LOGW(TAG, "Poignée de main : réponse inattendue à 06 : %s", hex_(r).c_str());
      this->next_handshake_at_ = millis() + HANDSHAKE_RETRY_MS;
      return;
    }
    uint32_t ta = ((uint32_t) r[1] << 24) | (r[2] << 16) | (r[3] << 8) | r[4];
    uint32_t b = ((uint32_t) r[5] << 24) | (r[6] << 16) | (r[7] << 8) | r[8];
    if (ta != transform_(this->challenge_))
      ESP_LOGW(TAG, "Poignée de main : T(A) reçu %08X, attendu %08X (transformée différente ?)", (unsigned) ta,
               (unsigned) transform_(this->challenge_));
    else
      ESP_LOGI(TAG, "Poignée de main : T(A) correct, défi du programmateur %08X", (unsigned) b);
    uint32_t tb = transform_(b);
    this->queue_.push_front(Request{{0x07, (uint8_t) (tb >> 24), (uint8_t) (tb >> 16), (uint8_t) (tb >> 8), (uint8_t) tb}, false});
    return;
  }
  if (op == 0x07) {
    if (r[0] != 0x87) {
      ESP_LOGW(TAG, "Poignée de main refusée : %s", hex_(r).c_str());
      this->next_handshake_at_ = millis() + HANDSHAKE_RETRY_MS;
      return;
    }
    ESP_LOGI(TAG, "Session ouverte (87)");
    this->session_ok_ = true;
    this->misses_in_row_ = 0;
    // Same sequence as the LNK2 after its handshake (recorded on 01/11/2026, 15:40)
    this->enqueue_({0x04, 0x04});
    this->enqueue_({0x0D});
    this->enqueue_({0x0E, 0x02});
    this->enqueue_({0x02});
    this->enqueue_({0x03, 0x00});
    this->update();
    this->config_requested_ = false;
    this->request_config();
    return;
  }
  if (r[0] == 0x01 && r.size() >= 2) {
    ESP_LOGI(TAG, "Commande %02X acceptée", r[1]);
    this->update();  // refresh the state right away
    return;
  }
  if (r[0] == 0x00) {
    ESP_LOGW(TAG, "Commande %02X refusée : %s", op, hex_(r).c_str());
    return;
  }
  if (op == 0x0C && r[0] == 0x8C) {
    this->handle_object_response_(r);
    return;
  }
  if (r[0] != (uint8_t) (op | 0x80)) {
    ESP_LOGW(TAG, "Réponse inattendue à %02X : %s", op, hex_(r).c_str());
    return;
  }
  char buf[48];
  switch (r[0]) {
    case 0x82:  // model and version
      if (r.size() >= 5) {
        this->model_known_ = true;
        unsigned model = (r[1] << 8) | r[2];
        snprintf(buf, sizeof(buf), "%s (0x%04X) v%u.%u", model == 0x0009 ? "ESP-ME3" : "inconnu", model, r[3], r[4]);
        if (this->model_ != nullptr)
          this->model_->publish_state(buf);
        if (model != 0x0009)
          ESP_LOGW(TAG, "Modèle 0x%04X : protocole relevé seulement sur l'ESP-ME3 (0x0009)", model);
      }
      break;
    case 0xBB:  // irrigation queue, pages 1 to 3
      if (r.size() >= 2 && r[1] >= 1 && r[1] <= 3)
        this->parse_queue_page_(r);
      else
        ESP_LOGI(TAG, "Réponse %s", hex_(r).c_str());
      break;
    case 0x83:  // available stations, page 0: 32-bit mask
      if (r.size() >= 6 && r[1] == 0x00) {
        uint32_t dispo = r[2] | (r[3] << 8) | (r[4] << 16) | ((uint32_t) r[5] << 24);
        int n = __builtin_popcount(dispo);
        if (n >= 1 && n <= 22)
          this->nzones_ = n;
      }
      break;
    case 0x90:  // time
      if (r.size() >= 4 && this->time_ != nullptr) {
        snprintf(buf, sizeof(buf), "%02u:%02u:%02u", r[1], r[2], r[3]);
        this->time_->publish_state(buf);
      }
      break;
    case 0x92:  // date: day, month (high nibble), year on 12 bits
      if (r.size() >= 4 && this->date_ != nullptr) {
        unsigned year = ((r[2] & 0x0F) << 8) | r[3];
        snprintf(buf, sizeof(buf), "%02u/%02u/%04u", r[1], r[2] >> 4, year);
        this->date_->publish_state(buf);
      }
      break;
    case 0xBF:  // active zones, page 0: 32-bit little-endian mask
      if (r.size() >= 6 && r[1] == 0x00) {
        uint32_t mask = r[2] | (r[3] << 8) | (r[4] << 16) | ((uint32_t) r[5] << 24);
        this->active_mask_ = mask;
        if (mask == 0) {
          this->run_zone_ = 0;
          this->run_left_ = 0;
          this->en_attente_[0].clear();
          this->en_attente_[1].clear();
        }
        std::string zones;
        int first = 0;
        for (int i = 0; i < 32; i++) {
          if (mask & (1u << i)) {
            if (!first)
              first = i + 1;
            if (!zones.empty())
              zones += ", ";
            zones += to_string(i + 1);
          }
        }
        if (this->active_zones_ != nullptr)
          this->active_zones_->publish_state(zones.empty() ? "aucune" : zones);
        if (this->active_zone_ != nullptr)
          this->active_zone_->publish_state(first);
      }
      break;
    case 0xB6:  // rain delay in days
      if (r.size() >= 3 && this->rain_delay_ != nullptr)
        this->rain_delay_->publish_state((r[1] << 8) | r[2]);
      break;
    case 0xBE:  // rain sensor
      if (r.size() >= 2 && this->rain_sensor_ != nullptr)
        this->rain_sensor_->publish_state(r[1] != 0);
      break;
    case 0xC8:  // irrigation state: 1 = AUTO, 0 = OFF
      if (r.size() >= 2 && this->irrigation_state_ != nullptr)
        this->irrigation_state_->publish_state(r[1]);
      break;
    default:
      ESP_LOGI(TAG, "Réponse %s", hex_(r).c_str());
      break;
  }
}

bool RainbirdME3::run_zone(int zone, int minutes) {
  if (!this->commands_allowed_ || zone < 1 || zone > 22 || minutes < 1 || minutes > 240) {
    ESP_LOGW(TAG, "Lancement zone %d / %d min refusé (commandes %s)", zone, minutes,
             this->commands_allowed_ ? "autorisées, valeurs hors limites" : "non autorisées");
    return false;
  }
  this->enqueue_({0x39, (uint8_t) (zone >> 8), (uint8_t) zone, (uint8_t) minutes}, true);
  return true;
}

bool RainbirdME3::stop_irrigation() {
  // Stop is always allowed, even without permission: it is the safety command.
  this->enqueue_({0x40}, true);
  return true;
}

bool RainbirdME3::run_program(int program) {
  if (!this->commands_allowed_ || program < 0 || program > 3) {
    ESP_LOGW(TAG, "Lancement programme %d refusé", program);
    return false;
  }
  this->enqueue_({0x38, (uint8_t) program}, true);
  return true;
}

bool RainbirdME3::advance_zone() {
  if (!this->commands_allowed_) {
    ESP_LOGW(TAG, "Zone suivante refusée (commandes non autorisées)");
    return false;
  }
  this->enqueue_({0x42, 0x00}, true);
  return true;
}

bool RainbirdME3::stack_zone(int zone, int minutes) {
  if (!this->commands_allowed_ || zone < 1 || zone > 22 || minutes < 1 || minutes > 240) {
    ESP_LOGW(TAG, "Ajout de la zone %d à la file refusé", zone);
    return false;
  }
  this->enqueue_({0x4B, (uint8_t) (zone >> 8), (uint8_t) zone, (uint8_t) minutes}, true);
  return true;
}

bool RainbirdME3::test_all_zones(int minutes) {
  if (!this->commands_allowed_ || minutes < 1 || minutes > 100) {
    ESP_LOGW(TAG, "Test de toutes les zones refusé");
    return false;
  }
  this->enqueue_({0x3A, (uint8_t) minutes}, true);
  return true;
}

bool RainbirdME3::set_clock(int hour, int minute, int second, int day, int month, int year) {
  if (!this->commands_allowed_ || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59 ||
      day < 1 || day > 31 || month < 1 || month > 12 || year < 2024 || year > 2099) {
    ESP_LOGW(TAG, "Mise à l'heure refusée");
    return false;
  }
  this->enqueue_({0x11, (uint8_t) hour, (uint8_t) minute, (uint8_t) second}, true);
  this->enqueue_({0x13, (uint8_t) day, (uint8_t) ((month << 4) | (year >> 8)), (uint8_t) year}, true);
  return true;
}

std::vector<uint8_t> RainbirdME3::parse_hex_(const std::string &hex) {
  std::vector<uint8_t> p;
  unsigned v;
  const char *s = hex.c_str();
  int n;
  while (*s == ' ')
    s++;
  while (sscanf(s, "%2x%n", &v, &n) == 1) {
    p.push_back((uint8_t) v);
    s += n;
    while (*s == ' ')
      s++;
  }
  return p;
}

void RainbirdME3::manchester_begin_() {
  // Take the transmit pin back from the UART and set it to the Manchester idle level (low)
  gpio_num_t tx = (gpio_num_t) this->tx_gpio_;
  esp_rom_gpio_connect_out_signal(tx, SIG_GPIO_OUT_IDX, false, false);
  gpio_set_direction(tx, GPIO_MODE_OUTPUT);
  gpio_set_level(tx, 0);
  delay(10);
}

void RainbirdME3::manchester_end_() {
  // Give the pin back to the UART (idle high)
  auto *idf = static_cast<uart::IDFUARTComponent *>(this->parent_);
  uart_port_t port = (uart_port_t) idf->get_hw_serial_number();
  gpio_set_level((gpio_num_t) this->tx_gpio_, 1);
  uart_set_pin(port, this->tx_gpio_, this->rx_gpio_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  delay(10);
  uart_flush_input(port);
  this->in_frame_ = false;
}

void RainbirdME3::test_manchester(const std::string &hex) {
  this->trace_fronts_ = true;
  std::vector<uint8_t> p = parse_hex_(hex);
  if (p.empty() || p.size() > 40) {
    ESP_LOGW(TAG, "TEST Manchester : trame invalide « %s »", hex.c_str());
    return;
  }
  ESP_LOGI(TAG, "TEST Manchester → %s", hex_(p).c_str());
  this->manchester_begin_();
  std::vector<uint8_t> r = this->manchester_xfer_(p, 150);
  this->manchester_end_();
  this->trace_fronts_ = false;
  ESP_LOGI(TAG, "TEST Manchester ← %s", r.empty() ? "(rien de valide)" : hex_(r).c_str());
}

void RainbirdME3::test_manchester_handshake() {
  this->trace_fronts_ = true;
  uint32_t a = random_uint32();
  ESP_LOGI(TAG, "TEST poignée de main Manchester, défi %08X", (unsigned) a);
  this->manchester_begin_();
  std::vector<uint8_t> r = this->manchester_xfer_({0x06, (uint8_t) (a >> 24), (uint8_t) (a >> 16), (uint8_t) (a >> 8), (uint8_t) a}, 150);
  if (r.size() < 9 || r[0] != 0x86) {
    this->manchester_end_();
    this->trace_fronts_ = false;
    ESP_LOGW(TAG, "TEST poignée de main Manchester : réponse à 06 = %s", r.empty() ? "(rien de valide)" : hex_(r).c_str());
    return;
  }
  uint32_t ta = ((uint32_t) r[1] << 24) | (r[2] << 16) | (r[3] << 8) | r[4];
  uint32_t b = ((uint32_t) r[5] << 24) | (r[6] << 16) | (r[7] << 8) | r[8];
  uint32_t tb = transform_(b);
  std::vector<uint8_t> r2 = this->manchester_xfer_({0x07, (uint8_t) (tb >> 24), (uint8_t) (tb >> 16), (uint8_t) (tb >> 8), (uint8_t) tb}, 150);
  this->manchester_end_();
  this->trace_fronts_ = false;
  ESP_LOGI(TAG, "TEST poignée de main Manchester : T(A) %s, défi B %08X, réponse à 07 = %s",
           ta == transform_(a) ? "correct" : "FAUX", (unsigned) b, r2.empty() ? "(rien de valide)" : hex_(r2).c_str());
}

void RainbirdME3::test_uart(const std::string &hex) {
  std::vector<uint8_t> p = parse_hex_(hex);
  if (p.empty() || p.size() > 40) {
    ESP_LOGW(TAG, "TEST UART : trame invalide « %s »", hex.c_str());
    return;
  }
  if (p[0] == 0x06 && p.size() == 1) {  // « 06 » alone: random challenge
    uint32_t a = random_uint32();
    p = {0x06, (uint8_t) (a >> 24), (uint8_t) (a >> 16), (uint8_t) (a >> 8), (uint8_t) a};
  }
  ESP_LOGI(TAG, "TEST UART → %s", hex_(p).c_str());
  this->transmit_(p);
}

bool RainbirdME3::send_hex(const std::string &hex, bool is_write) {
  if (is_write && !this->commands_allowed_) {
    ESP_LOGW(TAG, "Envoi brut en écriture refusé (commandes non autorisées)");
    return false;
  }
  std::vector<uint8_t> p;
  unsigned v;
  const char *s = hex.c_str();
  int n;
  while (sscanf(s, "%2x%n", &v, &n) == 1) {
    p.push_back((uint8_t) v);
    s += n;
    while (*s == ' ')
      s++;
  }
  if (p.empty() || p.size() > 250) {
    ESP_LOGW(TAG, "Payload brut invalide : %s", hex.c_str());
    return false;
  }
  this->enqueue_(p, is_write);
  return true;
}

}  // namespace rainbird_me3
}  // namespace esphome
