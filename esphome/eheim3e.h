// Protocole EHEIM professionel 3e (2074 / 2076 / 2078)
// Reconstruit depuis EHEIM ControlCenter 2.3 et validé sur un 2076, firmware 3.80.
//
// Trame : 0x55, adresse (0x60 = pompe, 0xFF = lecture firmware), longueur totale,
//         commande, données. Pas de checksum. 115200 bauds, 8N1, RS485 half-duplex.
#pragma once
#include "esphome.h"
#include <vector>
#include <cstring>
#include <deque>

namespace eheim {

static const uint8_t START = 0x55;
static const uint8_t DEV_PUMP = 0x60;
static const uint8_t DEV_BCAST = 0xFF;

static const uint8_t CMD_GET_DEVICEID = 0x01;     // réponse cmd 0xFF : firmware data[0].data[1]
static const uint8_t CMD_GET_ACTUALDATA = 0x13;   // réponse cmd 0x10 : 55 octets
static const uint8_t CMD_GET_PUMPVERSION = 0x16;  // réponse 0x90 0x16 <type-2000>
static const uint8_t CMD_SET_CONSTANTFLOW = 0x28; // data[0] = palier 0..14, réponse 0x90 0x28
static const uint8_t CMD_SET_KEY = 0x22;          // data[0] = touche (2 = +, 4 = -)
static const uint8_t CMD_SET_PUMP = 0x24;         // data = mode (uint16 LE)
static const uint8_t CMD_SET_PULSE = 0x25;        // durée haute, durée basse (int32 LE, s), palier haut, palier bas
static const uint8_t CMD_SET_BIO = 0x26;          // palier nuit, palier jour, minutes avant le début (uint16 LE)
static const uint8_t CMD_SET_TIME = 0x4A;         // heure, minute, heure début bio, minute début bio (trame de 68 octets)
static const uint8_t KEY_UP = 2;
static const uint8_t KEY_DOWN = 4;

static const uint16_t PM_NORMAL = 0x0001;
static const uint16_t PM_BIO = 0x0004;
static const uint16_t PM_PULSE = 0x0008;
static const uint16_t PM_MANUAL = 0x0010;

// Bits du mot de mode (enum PumpModes du logiciel Eheim)
static const uint16_t PM_ROTOR_ERR = 0x0100;
static const uint16_t PM_AIR_ERR = 0x0200;
static const uint16_t PM_TEMP_ERR = 0x0400;

// Structure FilterData (Pack=1, little-endian), 51 octets après l'en-tête de 4 octets
struct __attribute__((packed)) Data {
  int16_t frequenz, frequenz_soll, current, peak_current, calc_current, dfs_faktor, dfs;
  uint8_t drehzahl, drehrichtung, pulsbreite, pumpe_on, key_state, sensor;
  int16_t led_data, pumpe_mode;
  uint8_t menu_mode, status, soll_step, dfs_calc_diff;
  int16_t regler_dfs_soll, regler_freq_calc, regler_freq_set, dfs_stunde, dfs_tag, winkel;
  int32_t run_time, actual_time;
  int16_t wartung_hours;
  uint8_t wartung_month;
};
static_assert(sizeof(Data) == 51, "taille FilterData incorrecte");

// Débit en l/h par palier, index = palier + 1 (FilterParametersHelper.CalculateFlow)
static const uint16_t FLOW_2074[16] = {585, 400, 440, 480, 515, 550, 585, 620, 650, 680, 710, 740, 770, 800, 830, 860};
static const uint16_t FLOW_2076[16] = {650, 400, 460, 515, 565, 610, 650, 690, 730, 770, 805, 840, 875, 910, 945, 980};
static const uint16_t FLOW_2078[16] = {700, 400, 470, 540, 600, 650, 700, 745, 785, 825, 865, 905, 945, 985, 1025, 1065};

inline int flow_lh(int type, int step) {
  int i = step + 1;
  if (i < 0 || i > 15) return 0;
  if (type == 2074) return FLOW_2074[i];
  if (type == 2078) return FLOW_2078[i];
  return FLOW_2076[i];
}

inline uint8_t last_tx[5] = {0};
inline uint8_t last_tx_len = 5;
// Tampon de réception conservé entre deux lectures (une réponse peut arriver en plusieurs morceaux)
inline std::vector<uint8_t> rxbuf;

// Vide le tampon de réception puis envoie une trame de 5 octets
inline void send(esphome::uart::UARTComponent *u, uint8_t dev, uint8_t cmd, uint8_t data = 0) {
  uint8_t b;
  while (u->available()) u->read_byte(&b);
  rxbuf.clear();
  uint8_t f[5] = {START, dev, 5, cmd, data};
  memcpy(last_tx, f, 5);
  last_tx_len = 5;
  u->write_array(f, 5);
  u->flush();
}

// Envoie une trame complète (adresse pompe), longueur totale = len, données complétées par des zéros
inline void send_frame(esphome::uart::UARTComponent *u, uint8_t cmd, const std::vector<uint8_t> &data, uint8_t len) {
  uint8_t b;
  while (u->available()) u->read_byte(&b);
  rxbuf.clear();
  std::vector<uint8_t> f(len, 0);
  f[0] = START; f[1] = DEV_PUMP; f[2] = len; f[3] = cmd;
  for (size_t i = 0; i < data.size() && 4 + i < len; i++) f[4 + i] = data[i];
  memset(last_tx, 0, 5);
  memcpy(last_tx, f.data(), len < 5 ? len : 5);
  last_tx_len = len;
  u->write_array(f.data(), len);
  u->flush();
}

// File d'attente des commandes d'écriture (envoyées une par une, entre deux lectures)
struct Cmd { uint8_t cmd; std::vector<uint8_t> data; uint8_t len; };
inline std::deque<Cmd> queue;
inline void enqueue(uint8_t cmd, std::vector<uint8_t> data, uint8_t len) { queue.push_back({cmd, data, len}); }
inline void q_constant_flow(int step) { enqueue(CMD_SET_CONSTANTFLOW, {(uint8_t) step}, 5); }
inline void q_pump_mode(uint16_t m) { enqueue(CMD_SET_PUMP, {(uint8_t) (m & 0xFF), (uint8_t) (m >> 8)}, 6); }
inline void q_key(uint8_t k) { enqueue(CMD_SET_KEY, {k}, 5); }
inline void q_pulse(int high_step, int low_step, int32_t high_s, int32_t low_s) {
  std::vector<uint8_t> d(10, 0);
  memcpy(&d[0], &high_s, 4);
  memcpy(&d[4], &low_s, 4);
  d[8] = (uint8_t) high_step; d[9] = (uint8_t) low_step;
  enqueue(CMD_SET_PULSE, d, 14);
}
inline void q_bio(int day_step, int night_step, int start_h, int start_m, int now_h, int now_m) {
  int now = now_h * 60 + now_m, start = start_h * 60 + start_m;
  uint16_t delta = (uint16_t) (((start - now) % 1440 + 1440) % 1440);
  enqueue(CMD_SET_BIO, {(uint8_t) night_step, (uint8_t) day_step, (uint8_t) (delta & 0xFF), (uint8_t) (delta >> 8)}, 8);
  enqueue(CMD_SET_TIME, {(uint8_t) now_h, (uint8_t) now_m, (uint8_t) start_h, (uint8_t) start_m}, 68);
}

// Lit ce qui est arrivé, découpe en trames complètes et retire l'écho de notre propre envoi
inline std::vector<std::vector<uint8_t>> read_frames(esphome::uart::UARTComponent *u) {
  uint8_t b;
  while (u->available() && rxbuf.size() < 1024) {
    u->read_byte(&b);
    rxbuf.push_back(b);
  }
  std::vector<std::vector<uint8_t>> frames;
  size_t i = 0;
  while (i + 3 <= rxbuf.size()) {
    if (rxbuf[i] == START && (rxbuf[i + 1] == DEV_PUMP || rxbuf[i + 1] == DEV_BCAST) && rxbuf[i + 2] >= 4) {
      size_t n = rxbuf[i + 2];
      if (i + n > rxbuf.size()) break;               // trame incomplète : on attend la suite
      std::vector<uint8_t> f(rxbuf.begin() + i, rxbuf.begin() + i + n);
      bool echo = (n == last_tx_len && memcmp(f.data(), last_tx, 5) == 0);
      if (!echo) frames.push_back(f);
      i += n;
    } else {
      i++;
    }
  }
  rxbuf.erase(rxbuf.begin(), rxbuf.begin() + i);
  if (rxbuf.size() > 512) rxbuf.clear();
  return frames;
}

}  // namespace eheim
