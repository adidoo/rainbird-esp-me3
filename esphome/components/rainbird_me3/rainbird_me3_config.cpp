// ESP-ME3 configuration through the object protocol (0C / 8C envelope): programs, start times, durations,
// seasonal adjustment, rain delay, rain sensor. Formats recorded on the LNK2 and the app on 01/11/2026
// (see ../../PROTOCOLE_ESP-ME3.md § 4). Writes use the same form as the app's.
#include "rainbird_me3.h"

#include <algorithm>
#include <cstdio>

#include "esphome/core/log.h"

namespace esphome {
namespace rainbird_me3 {

static const char *const TAG = "rainbird_me3";

static const uint8_t ENVELOPE[] = {0x0C, 0x20, 0x00, 0x01, 0x00, 0x08, 0x05, 0x00, 0x00, 0x00, 0x00,
                                   0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00};
static const size_t REPLY_HEADER = 21;  // 8C 20 00 + 18 address bytes, then 16-bit length

static void le16(std::vector<uint8_t> &v, uint16_t x) {
  v.push_back(x & 0xFF);
  v.push_back(x >> 8);
}

void RainbirdME3::object_request_(const std::vector<uint8_t> &content, bool write) {
  std::vector<uint8_t> p(ENVELOPE, ENVELOPE + sizeof(ENVELOPE));
  le16(p, content.size());
  p.insert(p.end(), content.begin(), content.end());
  this->enqueue_(std::move(p), write);
}

void RainbirdME3::request_config() {
  if (this->config_requested_ || !this->session_ok_)
    return;
  this->config_requested_ = true;
  // Same reads as the LNK2 at start-up
  this->object_request_({0x07, 0x0B, 0x04, 0x14, 0, 0, 0x0D, 0, 0, 0x18, 0, 1, 0, 0, 3, 0, 0x13, 0, 1, 0, 0, 3, 0});
  this->object_request_(
      {0x07, 0x0B, 0x03, 0x12, 0, 1, 0, 0, 3, 0, 0x11, 0, 1, 0, 0, 3, 0, 0x10, 0, 2, 0, 0, 3, 0, 0, 0, 6, 0});
  this->object_request_({0x07, 0x0B, 0x01, 0x1D, 0, 3, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 5, 0});
  for (uint8_t p = 0; p < 4; p++)
    this->object_request_({0x07, 0x0B, 0x01, 0x15, 0, 2, p, 0, p, 0, 0, 0, 0x15, 0});
  this->object_request_({0x07, 0x0B, 0x02, 0x0A, 0, 1, 0, 0, 0x15, 0, 0x0B, 0, 1, 0, 0, 0x15, 0});
}

void RainbirdME3::store_(uint16_t obj, unsigned i0, unsigned i1, unsigned i2, uint32_t v) {
  Config &c = this->cfg_;
  switch (obj) {
    case 0x14:
      c.rain_delay = v;
      break;
    case 0x0D:
      c.rain_sensor_enabled = v != 0;
      break;
    case 0x18:
      if (i0 < 4)
        c.seasonal[i0] = v;
      break;
    case 0x13:
      if (i0 < 4)
        c.freq[i0] = v;
      break;
    case 0x12:
      if (i0 < 4)
        c.interval[i0] = v;
      break;
    case 0x11:
      if (i0 < 4)
        c.remaining[i0] = v;
      break;
    case 0x10:
      if (i0 < 4 && i1 < 7)
        c.days[i0][i1] = v != 0;
      break;
    case 0x1D:
      if (i0 < 4 && i2 < 6)
        c.starts[i0][i2] = v;
      break;
    case 0x15:
      if (i0 < 4 && i1 < 22)
        c.dur[i0][i1] = v;
      break;
    case 0x0A:
      if (i0 < 22)
        c.cycle[i0] = v;
      break;
    case 0x0B:
      if (i0 < 22)
        c.soak[i0] = v;
      break;
    default:
      break;
  }
}

void RainbirdME3::handle_object_response_(const std::vector<uint8_t> &r) {
  if (r.size() < REPLY_HEADER + 3) {
    ESP_LOGW(TAG, "Réponse d'objets trop courte : %s", hex_(r).c_str());
    return;
  }
  size_t len = r[REPLY_HEADER] | (r[REPLY_HEADER + 1] << 8);
  if (REPLY_HEADER + 2 + len > r.size() || len < 2) {
    ESP_LOGW(TAG, "Réponse d'objets de longueur incohérente : %s", hex_(r).c_str());
    return;
  }
  const uint8_t *c = &r[REPLY_HEADER + 2];
  if (c[0] == 0x06) {  // write acknowledged: read everything again to show the real state
    ESP_LOGI(TAG, "Écriture de configuration acceptée");
    this->reread_pending_ = true;
    return;
  }
  if (c[0] != 0x08 || len < 3) {
    ESP_LOGW(TAG, "Réponse d'objets inattendue : %s", hex_(r).c_str());
    return;
  }
  size_t pos = 3;
  for (unsigned n = 0; n < c[2]; n++) {
    if (pos + 3 > len)
      return;
    uint16_t obj = c[pos] | (c[pos + 1] << 8);
    unsigned nd = c[pos + 2];
    pos += 3;
    if (nd > 3 || pos + 4 * nd + 1 > len) {
      ESP_LOGW(TAG, "Objet %02X : descripteur illisible", obj);
      return;
    }
    unsigned a[3] = {0, 0, 0}, cnt[3] = {1, 1, 1};
    for (unsigned d = 0; d < nd; d++) {
      unsigned debut = c[pos] | (c[pos + 1] << 8), fin = c[pos + 2] | (c[pos + 3] << 8);
      pos += 4;
      if (fin < debut || fin - debut > 63) {
        ESP_LOGW(TAG, "Objet %02X : plage illisible", obj);
        return;
      }
      a[d] = debut;
      cnt[d] = fin - debut + 1;
    }
    unsigned t = c[pos++];
    size_t total = (size_t) cnt[0] * cnt[1] * cnt[2];
    if ((t != 1 && t != 2 && t != 4) || pos + total * t > len) {
      ESP_LOGW(TAG, "Objet %02X : valeurs illisibles (type %u)", obj, t);
      return;
    }
    for (size_t k = 0; k < total; k++) {  // the last dimension varies fastest
      uint32_t v = 0;
      for (unsigned b = 0; b < t; b++)
        v |= (uint32_t) c[pos + k * t + b] << (8 * b);
      this->store_(obj, a[0] + k / (cnt[1] * cnt[2]), a[1] + (k / cnt[2]) % cnt[1], a[2] + k % cnt[2], v);
    }
    pos += total * t;
    if (obj == 0x0B) {  // last read of the series
      if (!this->config_loaded_)
        ESP_LOGI(TAG, "Configuration du programmateur lue");
      this->config_loaded_ = true;
      this->config_requested_ = false;
    }
  }
}

bool RainbirdME3::can_write_(const char *what) {
  if (this->commands_allowed_ && this->config_loaded_ && this->session_ok_)
    return true;
  ESP_LOGW(TAG, "%s refusé : %s", what,
           !this->commands_allowed_ ? "commandes non autorisées"
                                    : !this->session_ok_ ? "pas de session" : "configuration pas encore lue");
  return false;
}

bool RainbirdME3::set_seasonal(int p, int pct) {
  if (p < 0 || p > 3 || pct < 0 || pct > 300 || !this->can_write_("Ajustement saisonnier"))
    return false;
  this->cfg_.seasonal[p] = pct;
  this->object_request_(
      {0x05, 0x0B, 0x01, 0x18, 0, 1, (uint8_t) p, 0, (uint8_t) p, 0, 0x02, (uint8_t) pct, (uint8_t) (pct >> 8)}, true);
  return true;
}

bool RainbirdME3::set_program_byte_(uint8_t obj, int p, int v, const char *what) {
  if (p < 0 || p > 3 || v < 0 || v > 255 || !this->can_write_(what))
    return false;
  this->store_(obj, p, 0, 0, v);
  this->object_request_({0x05, 0x0B, 0x01, obj, 0, 1, (uint8_t) p, 0, (uint8_t) p, 0, 0x01, (uint8_t) v}, true);
  return true;
}

bool RainbirdME3::set_day(int p, int day, bool on) {
  if (p < 0 || p > 3 || day < 0 || day > 6 || !this->can_write_("Jour"))
    return false;
  this->cfg_.days[p][day] = on;
  std::vector<uint8_t> c = {0x05, 0x0B, 0x01, 0x10, 0, 2, (uint8_t) p, 0, (uint8_t) p, 0, 0, 0, 6, 0, 0x01};
  for (int d = 0; d < 7; d++)
    c.push_back(this->cfg_.days[p][d] ? 1 : 0);
  this->object_request_(c, true);
  return true;
}

bool RainbirdME3::set_start(int p, int index, int minutes) {
  if (p < 0 || p > 3 || index < 0 || index > 5 || minutes < 0 || minutes > 1440 || !this->can_write_("Départ"))
    return false;
  this->cfg_.starts[p][index] = minutes;
  std::vector<uint8_t> c = {0x05, 0x0B, 0x01, 0x1D, 0, 3, (uint8_t) p, 0, (uint8_t) p, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0x02};
  for (int i = 0; i < 6; i++)
    le16(c, this->cfg_.starts[p][i]);
  this->object_request_(c, true);
  return true;
}

bool RainbirdME3::set_duration(int p, int zone, int seconds) {
  if (p < 0 || p > 3 || zone < 1 || zone > this->nzones_ || seconds < 0 || seconds > 6 * 3600 ||
      !this->can_write_("Durée de zone"))
    return false;
  this->cfg_.dur[p][zone - 1] = seconds;
  std::vector<uint8_t> c = {0x05, 0x0B, 0x01, 0x15, 0, 2, (uint8_t) p, 0, (uint8_t) p, 0, 0, 0,
                            (uint8_t) (this->nzones_ - 1), 0, 0x04};
  for (int z = 0; z < this->nzones_; z++) {
    uint32_t v = this->cfg_.dur[p][z];
    c.push_back(v & 0xFF);
    c.push_back((v >> 8) & 0xFF);
    c.push_back((v >> 16) & 0xFF);
    c.push_back(v >> 24);
  }
  this->object_request_(c, true);
  return true;
}

bool RainbirdME3::set_cycle_soak(int zone, int cycle, int soak) {
  if (zone < 1 || zone > 22 || cycle < 0 || cycle > 255 || soak < 0 || soak > 255 ||
      !this->can_write_("Cycle et ressuyage"))
    return false;
  this->cfg_.cycle[zone - 1] = cycle;
  this->cfg_.soak[zone - 1] = soak;
  std::vector<uint8_t> c = {0x05, 0x0B, 0x02};
  for (int o = 0; o < 2; o++) {
    const uint8_t descr[] = {(uint8_t) (o ? 0x0B : 0x0A), 0, 1, 0, 0, 0x15, 0, 0x02};
    c.insert(c.end(), descr, descr + sizeof(descr));
    for (int z = 0; z < 22; z++)
      le16(c, o ? this->cfg_.soak[z] : this->cfg_.cycle[z]);
  }
  this->object_request_(c, true);
  return true;
}

bool RainbirdME3::set_rain_sensor_enabled(bool on) {
  if (!this->can_write_("Sonde de pluie"))
    return false;
  this->cfg_.rain_sensor_enabled = on;
  this->object_request_({0x05, 0x0B, 0x01, 0x0D, 0, 0, 0x01, (uint8_t) (on ? 1 : 0)}, true);
  return true;
}

bool RainbirdME3::set_rain_delay(int days) {
  if (days < 0 || days > 14 || !this->can_write_("Report"))
    return false;
  this->cfg_.rain_delay = days;
  this->object_request_({0x05, 0x0B, 0x01, 0x14, 0, 0, 0x01, (uint8_t) days}, true);
  this->enqueue_({0x36});
  return true;
}

std::string RainbirdME3::start_text(int p, int index) const {
  if (p < 0 || p > 3 || index < 0 || index > 5 || !this->config_loaded_ || this->cfg_.starts[p][index] >= 1440)
    return "--:--";
  char buf[8];
  snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned) (this->cfg_.starts[p][index] / 60),
           (unsigned) (this->cfg_.starts[p][index] % 60));
  return buf;
}

bool RainbirdME3::set_start_text(int p, int index, const std::string &text) {
  unsigned h, m;
  int minutes = 1440;  // anything that is not a valid time clears the start
  if (sscanf(text.c_str(), "%u:%u", &h, &m) == 2 && h < 24 && m < 60)
    minutes = h * 60 + m;
  return this->set_start(p, index, minutes);
}

std::string RainbirdME3::program_summary(int p) const {
  if (p < 0 || p > 3 || !this->config_loaded_)
    return "?";
  static const char *const JOURS[] = {"dim", "lun", "mar", "mer", "jeu", "ven", "sam"};
  const Config &c = this->cfg_;
  std::string out;
  char buf[40];
  for (int i = 0; i < 6; i++) {
    if (c.starts[p][i] < 1440) {
      snprintf(buf, sizeof(buf), "%s%02u:%02u", out.empty() ? "" : " ", (unsigned) (c.starts[p][i] / 60),
               (unsigned) (c.starts[p][i] % 60));
      out += buf;
    }
  }
  if (out.empty())
    return "inactif (aucun départ)";
  out += " · ";
  switch (c.freq[p]) {
    case 0:
      for (int d = 1; d <= 7; d++)  // Monday first for display
        if (c.days[p][d % 7])
          out += std::string(JOURS[d % 7]) + " ";
      break;
    case 2:
      out += "jours impairs ";
      break;
    case 3:
      out += "jours pairs ";
      break;
    case 4:
      snprintf(buf, sizeof(buf), "tous les %u j (reste %u) ", (unsigned) c.interval[p], (unsigned) c.remaining[p]);
      out += buf;
      break;
    default:
      snprintf(buf, sizeof(buf), "mode %u ", (unsigned) c.freq[p]);
      out += buf;
  }
  snprintf(buf, sizeof(buf), "· %u %% ·", (unsigned) c.seasonal[p]);
  out += buf;
  for (int z = 0; z < 22; z++) {
    if (c.dur[p][z]) {
      snprintf(buf, sizeof(buf), " Z%d %.0f min", z + 1, c.dur[p][z] / 60.0f);
      out += buf;
    }
  }
  if (out.size() > 250)
    out.resize(250);
  return out;
}

void RainbirdME3::parse_queue_page_(const std::vector<uint8_t> &r) {
  // BB <page> then 6-byte entries: <type> <zone> <32-bit LE seconds>; type 00 = empty.
  // Recorded on 01/11/2026: page 1 = running zone (remaining seconds, decreasing), pages 2-3 = waiting.
  std::vector<std::pair<uint8_t, uint32_t>> entries;
  for (size_t i = 2; i + 6 <= r.size(); i += 6) {
    if (r[i] == 0 || r[i + 1] == 0)
      continue;
    uint32_t s = r[i + 2] | (r[i + 3] << 8) | (r[i + 4] << 16) | ((uint32_t) r[i + 5] << 24);
    entries.push_back({r[i + 1], s});
  }
  if (r[1] == 1) {
    this->run_zone_ = entries.empty() ? 0 : entries[0].first;
    this->run_left_ = entries.empty() ? 0 : entries[0].second;
  } else {
    this->en_attente_[r[1] - 2] = entries;
  }
}

std::string RainbirdME3::queue_text() const {
  std::string out;
  char buf[24];
  if (this->run_zone_) {
    snprintf(buf, sizeof(buf), "%d:%d", this->run_zone_, this->run_left_);
    out += buf;
  }
  for (const auto &page : this->en_attente_)
    for (const auto &e : page) {
      snprintf(buf, sizeof(buf), "%s%u:%u", out.empty() ? "" : ";", (unsigned) e.first, (unsigned) e.second);
      out += buf;
    }
  if (out.size() > 250)
    out.resize(250);
  return out.empty() ? "-" : out;
}

std::string RainbirdME3::program_data(int p) const {
  if (p < 0 || p > 3 || !this->config_loaded_)
    return "?";
  const Config &c = this->cfg_;
  std::string out = "s=";
  char buf[24];
  bool first = true;
  for (int i = 0; i < 6; i++)
    if (c.starts[p][i] < 1440) {
      snprintf(buf, sizeof(buf), "%s%u", first ? "" : ",", (unsigned) c.starts[p][i]);
      out += buf;
      first = false;
    }
  snprintf(buf, sizeof(buf), ";f=%u;d=", (unsigned) c.freq[p]);
  out += buf;
  for (int d = 0; d < 7; d++)
    out += c.days[p][d] ? '1' : '0';
  snprintf(buf, sizeof(buf), ";i=%u;r=%u;p=%u;z=", (unsigned) c.interval[p], (unsigned) c.remaining[p],
           (unsigned) c.seasonal[p]);
  out += buf;
  first = true;
  for (int z = 0; z < 22; z++)
    if (c.dur[p][z]) {
      snprintf(buf, sizeof(buf), "%s%d:%u", first ? "" : ",", z + 1, (unsigned) c.dur[p][z]);
      out += buf;
      first = false;
    }
  if (out.size() > 250)
    out.resize(250);
  return out;
}

bool RainbirdME3::write_program(int p, int freq, int interval, int remaining, const std::string &jours,
                                const std::string &departs, const std::string &durees, int pct) {
  if (p < 0 || p > 3 || (freq != 0 && freq != 2 && freq != 3 && freq != 4) || interval < 1 || interval > 30 ||
      remaining < 0 || remaining > 30 || jours.size() != 7 || pct < 0 || pct > 300) {
    ESP_LOGW(TAG, "Programme %c : paramètres invalides", 'A' + p);
    return false;
  }
  if (!this->can_write_("Écriture du programme"))
    return false;
  // Read and check everything before writing anything
  bool days[7];
  for (int d = 0; d < 7; d++) {
    if (jours[d] != '0' && jours[d] != '1') {
      ESP_LOGW(TAG, "Programme %c : jours invalides « %s »", 'A' + p, jours.c_str());
      return false;
    }
    days[d] = jours[d] == '1';
  }
  std::vector<uint16_t> starts;
  for (size_t i = 0; i < departs.size();) {
    unsigned h, m;
    int n = 0;
    if (sscanf(departs.c_str() + i, "%u:%u%n", &h, &m, &n) != 2 || h > 23 || m > 59 || starts.size() >= 6) {
      ESP_LOGW(TAG, "Programme %c : départs invalides « %s »", 'A' + p, departs.c_str());
      return false;
    }
    starts.push_back(h * 60 + m);
    i += n;
    while (i < departs.size() && (departs[i] == ',' || departs[i] == ' '))
      i++;
  }
  std::sort(starts.begin(), starts.end());
  starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
  while (starts.size() < 6)
    starts.push_back(1440);
  uint32_t dur[22] = {0};
  for (size_t i = 0; i < durees.size();) {
    unsigned z, s;
    int n = 0;
    if (sscanf(durees.c_str() + i, "%u:%u%n", &z, &s, &n) != 2 || z < 1 || z > (unsigned) this->nzones_ ||
        s > 6 * 3600) {
      ESP_LOGW(TAG, "Programme %c : durées invalides « %s »", 'A' + p, durees.c_str());
      return false;
    }
    dur[z - 1] = s;
    i += n;
    while (i < durees.size() && (durees[i] == ',' || durees[i] == ' '))
      i++;
  }

  Config &c = this->cfg_;
  int ecrits = 0;
  if (c.seasonal[p] != pct) {
    this->set_seasonal(p, pct);
    ecrits++;
  }
  if (c.freq[p] != freq) {
    this->set_frequency(p, freq);
    ecrits++;
  }
  if (freq == 4 && c.interval[p] != interval) {
    this->set_interval(p, interval);
    ecrits++;
  }
  if (freq == 4 && c.remaining[p] != remaining) {
    this->set_remaining_days(p, remaining);
    ecrits++;
  }
  bool jours_changent = false;
  for (int d = 0; d < 7; d++)
    jours_changent |= c.days[p][d] != days[d];
  if (jours_changent) {
    for (int d = 1; d < 7; d++)
      c.days[p][d] = days[d];
    this->set_day(p, 0, days[0]);  // sends the 7 days
    ecrits++;
  }
  bool departs_changent = false;
  for (int i = 0; i < 6; i++)
    departs_changent |= c.starts[p][i] != starts[i];
  if (departs_changent) {
    for (int i = 1; i < 6; i++)
      c.starts[p][i] = starts[i];
    this->set_start(p, 0, starts[0]);  // sends the 6 start times
    ecrits++;
  }
  bool durees_changent = false;
  for (int z = 0; z < this->nzones_; z++)
    durees_changent |= c.dur[p][z] != dur[z];
  if (durees_changent) {
    for (int z = 1; z < this->nzones_; z++)
      c.dur[p][z] = dur[z];
    this->set_duration(p, 1, dur[0]);  // sends the durations of every zone
    ecrits++;
  }
  ESP_LOGI(TAG, "Programme %c : %d écriture(s) envoyée(s)", 'A' + p, ecrits);
  return true;
}

}  // namespace rainbird_me3
}  // namespace esphome
