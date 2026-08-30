#include "sim7670g.h"

#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>

#ifdef USE_ESP32
extern "C" {
#include "ml_cellular.h"
}
#endif


namespace esphome {
namespace sim7670g {

static const char *const TAG = "sim7670g";

// Global pointer for C callback → C++ instance bridge
static Sim7670gComponent *s_sim7670g_instance = nullptr;

static void modem_rx_callback(const uint8_t *data, size_t len, void *arg) {
  if (s_sim7670g_instance)
    s_sim7670g_instance->feed_modem_data(data, len);
}
// ---------------------------------------------------------------------------
// NMEA Parser
// ---------------------------------------------------------------------------

static bool check_nmea_checksum(const char *sentence, size_t len) {
  const char *star = nullptr;
  for (size_t i = 0; i < len; i++) {
    if (sentence[i] == '*') {
      star = sentence + i;
      break;
    }
  }
  if (!star || star + 3 > sentence + len)
    return false;

  uint8_t cs = 0;
  for (const char *p = sentence + 1; p < star; p++)
    cs ^= static_cast<uint8_t>(*p);

  uint8_t expected;
  if (sscanf(star + 1, "%2x", &expected) != 1)
    return false;
  return cs == expected;
}

void Sim7670gComponent::feed_nMEA(const uint8_t *data, size_t len) {
  if (!this->gps_enabled_)
    return;

  for (size_t i = 0; i < len; i++) {
    char c = static_cast<char>(data[i]);

    if (c == '$') {
      this->nmea_buf_len_ = 0;
      this->nmea_buf_[0] = '\0';
    }

    if (this->nmea_buf_len_ < sizeof(this->nmea_buf_) - 1 &&
        (c == '$' || this->nmea_buf_len_ > 0)) {
      this->nmea_buf_[this->nmea_buf_len_++] = c;
      this->nmea_buf_[this->nmea_buf_len_] = '\0';
    }

    if (c == '\n' && this->nmea_buf_len_ > 5) {
      parse_nmea_line(this->nmea_buf_);
      this->nmea_buf_len_ = 0;
      this->nmea_buf_[0] = '\0';
    }
  }
}

bool Sim7670gComponent::parse_nmea_line(const char *line) {
  size_t len = strlen(line);
  if (len < 6)
    return false;

  if (!check_nmea_checksum(line, len))
    return false;

  const char *type = line + 1;  // skip '$'
  const char *star = strchr(line, '*');

  auto extract_fields = [line, star, type](char *out, size_t out_size) {
    // Fields start after "GGA," or "RMC," (talker=2 + type=3 + comma=1 = 6)
    const char *fields_start = type + 6;
    size_t fields_len = star ? (size_t)(star - fields_start) : strlen(fields_start);
    while (fields_len > 0 && (line[fields_start - line + fields_len - 1] == '\r' ||
                               line[fields_start - line + fields_len - 1] == '\n'))
      fields_len--;
    if (fields_len >= out_size)
      fields_len = out_size - 1;
    memcpy(out, fields_start, fields_len);
    out[fields_len] = '\0';
  };

  char fields[128];

  if (strncmp(type, "GNGGA", 5) == 0 || strncmp(type, "GPGGA", 5) == 0) {
    extract_fields(fields, sizeof(fields));
    return parse_gga(fields);
  }
  if (strncmp(type, "GNRMC", 5) == 0 || strncmp(type, "GPRMC", 5) == 0) {
    extract_fields(fields, sizeof(fields));
    return parse_rmc(fields);
  }
  return false;
}

/// Parse GGA fields (CSV): time,lat,N/S,lon,E/W,quality,satellites,HDOP,alt,M,...
bool Sim7670gComponent::parse_gga(const char *fields_csv) {
  char buf[128];
  strncpy(buf, fields_csv, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char *tok[16];
  int ntok = 0;
  char *saveptr = nullptr;
  for (char *t = strtok_r(buf, ",", &saveptr); t && ntok < 16;
       t = strtok_r(nullptr, ",", &saveptr)) {
    tok[ntok++] = t;
  }

  // tok[0]=time, 1=lat, 2=N/S, 3=lon, 4=E/W, 5=quality, 6=satellites,
  // 7=HDOP, 8=altitude(m), 9=M, ...
  if (ntok < 9)
    return false;

  int quality = atoi(tok[5]);
  if (quality == 0)
    return false;

  double lat_raw = atof(tok[1]);
  if (lat_raw == 0.0)
    return false;
  int lat_deg = static_cast<int>(lat_raw / 100);
  double lat_min = lat_raw - lat_deg * 100;
  double lat = (lat_deg + lat_min / 60.0) * (strcmp(tok[2], "S") == 0 ? -1.0 : 1.0);

  double lon_raw = atof(tok[3]);
  if (lon_raw == 0.0)
    return false;
  int lon_deg = static_cast<int>(lon_raw / 100);
  double lon_min = lon_raw - lon_deg * 100;
  double lon = (lon_deg + lon_min / 60.0) * (strcmp(tok[4], "W") == 0 ? -1.0 : 1.0);

  int satellites = atoi(tok[6]);
  double hdop = atof(tok[7]);
  double alt = atof(tok[8]);

  if (this->latitude_sensor_)
    this->latitude_sensor_->publish_state(static_cast<float>(lat));
  if (this->longitude_sensor_)
    this->longitude_sensor_->publish_state(static_cast<float>(lon));
  if (this->altitude_sensor_)
    this->altitude_sensor_->publish_state(static_cast<float>(alt));
  if (this->satellites_sensor_)
    this->satellites_sensor_->publish_state(satellites);
  if (this->hdop_sensor_)
    this->hdop_sensor_->publish_state(static_cast<float>(hdop));

  ESP_LOGD(TAG, "GPS GGA: lat=%.6f lon=%.6f alt=%.1f sat=%d hdop=%.2f quality=%d",
           lat, lon, alt, satellites, hdop, quality);
  return true;
}

/// Parse RMC fields (CSV): time,status,lat,N/S,lon,E/W,speed,course,date,...
bool Sim7670gComponent::parse_rmc(const char *fields_csv) {
  char buf[128];
  strncpy(buf, fields_csv, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char *tok[16];
  int ntok = 0;
  char *saveptr = nullptr;
  for (char *t = strtok_r(buf, ",", &saveptr); t && ntok < 16;
       t = strtok_r(nullptr, ",", &saveptr)) {
    tok[ntok++] = t;
  }

  // tok[0]=time, 1=status, 2=lat, 3=N/S, 4=lon, 5=E/W, 6=speed(knots),
  // 7=true_course, 8=date(ddmmyy), 9=mag_var, 10=mag_dir, 11=mode
  if (ntok < 9)
    return false;

  if (strcmp(tok[1], "A") != 0)
    return false;

  double speed_knots = atof(tok[6]);
  double speed_kmh = speed_knots * 1.852;

  if (this->speed_sensor_)
    this->speed_sensor_->publish_state(static_cast<float>(speed_kmh));

  if (this->fix_status_sensor_) {
    const char *mode = (ntok > 11 && tok[11][0]) ? tok[11] : "";
    const char *status_str = "No Fix";
    if (mode[0] == 'A' || mode[0] == '3')
      status_str = "3D Fix";
    else if (mode[0] == '2')
      status_str = "2D Fix";
    else if (mode[0] == '1')
      status_str = "GPS Fix";
    else
      status_str = "Fix";
    this->fix_status_sensor_->publish_state(status_str);
  }

  if (this->datetime_sensor_) {
    char dt[32];
    int dd, mm, yy;
    if (sscanf(tok[8], "%2d%2d%2d", &dd, &mm, &yy) == 3) {
      int year = (yy < 50) ? 2000 + yy : 1900 + yy;
      int hh, mn, ss;
      if (sscanf(tok[0], "%2d%2d%2d", &hh, &mn, &ss) == 3) {
        snprintf(dt, sizeof(dt), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                 year, mm, dd, hh, mn, ss);
        this->datetime_sensor_->publish_state(dt);
      }
    }
  }

  ESP_LOGD(TAG, "GPS RMC: speed=%.1f km/h", speed_kmh);
  return true;
}

/// Parse AT+CGPSINFO response:
/// +CGPSINFO: lat_ddmm.mm,lon_ddmm.mm,time,alt,speed,hdop,vsat,usat
void Sim7670gComponent::parse_cgpsinfo(const char *resp) {
  // Response format: +CGPSINFO: lat,lon,time,alt,speed,hdop,vsat,usat
  // Example: +CGPSINFO: 4042.3456,N,7400.1234,W,120000,100.5,12.3,2.5,8,10
  const char *prefix = strstr(resp, "+CGPSINFO:");
  if (!prefix)
    return;

  const char *data = prefix + 10;  // skip "+CGPSINFO:"

  char buf[256];
  strncpy(buf, data, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  // Remove trailing \r\n
  size_t len = strlen(buf);
  while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n'))
    buf[--len] = '\0';

  char *tok[16];
  int ntok = 0;
  char *saveptr = nullptr;
  for (char *t = strtok_r(buf, ",", &saveptr); t && ntok < 16;
       t = strtok_r(nullptr, ",", &saveptr)) {
    tok[ntok++] = t;
  }

  // tok[0]=lat, 1=N/S, 2=lon, 3=E/W, 4=time, 5=alt, 6=speed, 7=hdop,
  // 8=vsat, 9=usat
  if (ntok < 10)
    return;

  double lat_raw = atof(tok[0]);
  if (lat_raw == 0.0)
    return;
  int lat_deg = static_cast<int>(lat_raw / 100);
  double lat_min = lat_raw - lat_deg * 100;
  double lat = (lat_deg + lat_min / 60.0) * (strcmp(tok[1], "S") == 0 ? -1.0 : 1.0);

  double lon_raw = atof(tok[2]);
  if (lon_raw == 0.0)
    return;
  int lon_deg = static_cast<int>(lon_raw / 100);
  double lon_min = lon_raw - lon_deg * 100;
  double lon = (lon_deg + lon_min / 60.0) * (strcmp(tok[3], "W") == 0 ? -1.0 : 1.0);

  double alt = atof(tok[5]);
  double speed_kmh = atof(tok[6]);  // already in km/h for CGPSINFO
  double hdop = atof(tok[7]);
  int satellites = atoi(tok[9]);  // used satellites

  if (this->latitude_sensor_)
    this->latitude_sensor_->publish_state(static_cast<float>(lat));
  if (this->longitude_sensor_)
    this->longitude_sensor_->publish_state(static_cast<float>(lon));
  if (this->altitude_sensor_)
    this->altitude_sensor_->publish_state(static_cast<float>(alt));
  if (this->speed_sensor_)
    this->speed_sensor_->publish_state(static_cast<float>(speed_kmh));
  if (this->satellites_sensor_)
    this->satellites_sensor_->publish_state(satellites);
  if (this->hdop_sensor_)
    this->hdop_sensor_->publish_state(static_cast<float>(hdop));

  // Parse time (UTC, format HHMMSS)
  if (this->datetime_sensor_ && strlen(tok[4]) >= 6) {
    char dt[32];
    int hh = (tok[4][0] - '0') * 10 + (tok[4][1] - '0');
    int mn = (tok[4][2] - '0') * 10 + (tok[4][3] - '0');
    int ss = (tok[4][4] - '0') * 10 + (tok[4][5] - '0');
    snprintf(dt, sizeof(dt), "%02d:%02d:%02dZ", hh, mn, ss);
    this->datetime_sensor_->publish_state(dt);
  }

  if (this->fix_status_sensor_) {
    const char *status_str = satellites > 0 ? "3D Fix" : "No Fix";
    this->fix_status_sensor_->publish_state(status_str);
  }

  ESP_LOGD(TAG, "CGPSINFO: lat=%.6f lon=%.6f alt=%.1f speed=%.1f sat=%d hdop=%.2f",
           lat, lon, alt, speed_kmh, satellites, hdop);
}

// ---------------------------------------------------------------------------
// Modem Data Feed (from microlink RX callback)
// ---------------------------------------------------------------------------

void Sim7670gComponent::feed_modem_data(const uint8_t *data, size_t len) {
  if (!this->gps_enabled_)
    return;

  // Feed NMEA parser for $GPGGA/$GNRMC sentences
  this->feed_nMEA(data, len);

  // Look for AT+CGPSINFO response
  char line_buf[256];
  size_t line_pos = 0;
  for (size_t i = 0; i < len; i++) {
    char c = static_cast<char>(data[i]);
    if (c == '\n' && line_pos > 0) {
      line_buf[line_pos] = '\0';
      parse_cgpsinfo(line_buf);
      line_pos = 0;
    } else if (line_pos < sizeof(line_buf) - 1) {
      line_buf[line_pos++] = c;
    }
  }
}

// ---------------------------------------------------------------------------
// ESPHome Component Lifecycle
// ---------------------------------------------------------------------------

void Sim7670gComponent::setup() {
#ifdef USE_ESP32
  // Battery ADC
  adc_oneshot_unit_init_cfg_t init_config = {
      .unit_id = ADC_UNIT_1,
      .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
      .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  if (adc_oneshot_new_unit(&init_config, &this->adc_handle_) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init ADC unit 1");
    this->mark_failed();
    return;
  }
  adc_oneshot_chan_cfg_t chan_config = {
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_12,
  };
  if (adc_oneshot_config_channel(this->adc_handle_,
                                  static_cast<adc_channel_t>(this->battery_adc_channel_),
                                  &chan_config) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure ADC channel %u", this->battery_adc_channel_);
    this->mark_failed();
    return;
  }
  this->adc_ready_ = true;
  ESP_LOGI(TAG, "Battery ADC ready on channel %u (divider %.2f)", this->battery_adc_channel_,
           this->voltage_divider_);
#endif

  // Register as microlink RX callback to receive modem UART data (NMEA + AT responses)
#ifdef USE_ESP32
  s_sim7670g_instance = this;
  ml_cellular_set_rx_callback(modem_rx_callback, nullptr);
  ESP_LOGI(TAG, "GPS NMEA parser enabled (RX callback registered with microlink)");
#else
  ESP_LOGI(TAG, "GPS NMEA parser enabled");
#endif
}

void Sim7670gComponent::loop() {
  uint32_t now = millis();
  if (now - this->last_update_ >= this->update_interval_ms_) {
    this->last_update_ = now;
    this->update_battery();
  }
}

void Sim7670gComponent::update_battery() {
#ifdef USE_ESP32
  if (!this->adc_ready_ || !this->battery_sensor_)
    return;

  int adc_val = 0;
  if (adc_oneshot_read(this->adc_handle_,
                       static_cast<adc_channel_t>(this->battery_adc_channel_),
                       &adc_val) != ESP_OK) {
    return;
  }

  // ADC12: 0-4095 maps to 0-1.1V (with ATTEN_DB_12)
  float v_raw = (adc_val / 4095.0f) * 1.1f * this->voltage_divider_;
  this->battery_sensor_->publish_state(v_raw);
#endif
}

void Sim7670gComponent::dump_config() {
  LOG_SENSOR("  ", "Latitude", this->latitude_sensor_);
  LOG_SENSOR("  ", "Longitude", this->longitude_sensor_);
  LOG_SENSOR("  ", "Altitude", this->altitude_sensor_);
  LOG_SENSOR("  ", "Speed", this->speed_sensor_);
  LOG_SENSOR("  ", "Satellites", this->satellites_sensor_);
  LOG_SENSOR("  ", "HDOP", this->hdop_sensor_);
  LOG_TEXT_SENSOR("  ", "Datetime", this->datetime_sensor_);
  LOG_TEXT_SENSOR("  ", "Fix Status", this->fix_status_sensor_);
  ESP_LOGCONFIG(TAG, "  GPS NMEA parser: %s", this->gps_enabled_ ? "enabled" : "disabled");
}

}  // namespace sim7670g
}  // namespace esphome
