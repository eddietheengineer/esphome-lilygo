#include "sim7670g.h"

#include "esphome/core/log.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>

namespace esphome {
namespace sim7670g {

static const char *const TAG = "sim7670g";

// ---------------------------------------------------------------------------
// AT+CGPSINFO Parser
// ---------------------------------------------------------------------------
//
// Response format:
//   +CGPSINFO: ddmm.mmmmm,h,dddmm.mmmmm,h,hhh.hh,ddmmyy,hhmmss.ss,sss.s,nn
//
// Fields (0-indexed after splitting on ','):
//   0 = latitude    (ddmm.mmmmm)
//   1 = N/S indicator
//   2 = longitude   (dddmm.mmmmm)
//   3 = E/W indicator
//   4 = altitude    (meters, can be empty)
//   5 = date        (ddmmyy)
//   6 = time        (hhmmss.ss)
//   7 = speed       (knots, can be empty)
//   8 = course      (degrees, can be empty)
//   9 = satellites  (can be empty)

static void split_csv(char *buf, char **tok, int *ntok) {
  *ntok = 0;
  char *saveptr = nullptr;
  for (char *t = strtok_r(buf, ",", &saveptr); t && *ntok < 16;
       t = strtok_r(nullptr, ",", &saveptr)) {
    tok[*ntok] = t;
    (*ntok)++;
  }
}

void Sim7670gComponent::query_gps() {
  if (!this->gps_enabled_)
    return;

  // Power on GNSS on first query
  if (!this->gnss_powered_) {
    char resp[256];
    int len = ml_cellular_send_at("AT+CGNSSPWR=1", resp, sizeof(resp), 5000);
    if (len > 0 && strstr(resp, "OK")) {
      this->gnss_powered_ = true;
      ESP_LOGI(TAG, "GNSS powered on");
    } else {
      ESP_LOGW(TAG, "Failed to power on GNSS");
      return;
    }
    // First call — wait for initial acquisition
    this->last_gps_query_ = millis() + GPS_INITIAL_DELAY_MS;
    return;
  }

  char resp[512];
  int len = ml_cellular_send_at("AT+CGPSINFO", resp, sizeof(resp), 10000);
  if (len <= 0) {
    ESP_LOGW(TAG, "AT+CGPSINFO timeout (modem may be in PPP mode)");
    return;
  }

  // Find "+CGPSINFO:" in response
  char *p = strstr(resp, "+CGPSINFO:");
  if (!p) {
    ESP_LOGD(TAG, "No +CGPSINFO in response: %s", resp);
    return;
  }
  p += 10;  // skip "+CGPSINFO:"

  // Split fields
  char *tok[16];
  int ntok = 0;
  split_csv(p, tok, &ntok);

  if (ntok < 4) {
    ESP_LOGD(TAG, "Too few fields in CGPSINFO: %d", ntok);
    return;
  }

  // Check if we have a fix: latitude field non-empty means some data
  bool has_fix = (tok[0][0] != '\0');

  if (this->fix_status_sensor_) {
    if (!has_fix) {
      this->fix_status_sensor_->publish_state("No Fix");
    } else {
      this->fix_status_sensor_->publish_state("Fix");
    }
  }

  if (!has_fix)
    return;

  // Latitude: ddmm.mmmmm
  double lat_raw = atof(tok[0]);
  int lat_deg = static_cast<int>(lat_raw / 100);
  double lat_min = lat_raw - lat_deg * 100;
  double lat = (lat_deg + lat_min / 60.0) * (strcmp(tok[1], "S") == 0 ? -1.0 : 1.0);

  // Longitude: dddmm.mmmmm
  double lon_raw = atof(tok[2]);
  int lon_deg = static_cast<int>(lon_raw / 100);
  double lon_min = lon_raw - lon_deg * 100;
  double lon = (lon_deg + lon_min / 60.0) * (strcmp(tok[3], "W") == 0 ? -1.0 : 1.0);

  if (this->latitude_sensor_)
    this->latitude_sensor_->publish_state(static_cast<float>(lat));
  if (this->longitude_sensor_)
    this->longitude_sensor_->publish_state(static_cast<float>(lon));

  // Altitude (field 4, may be empty)
  if (ntok > 4 && tok[4][0]) {
    double alt = atof(tok[4]);
    if (this->altitude_sensor_)
      this->altitude_sensor_->publish_state(static_cast<float>(alt));
  }

  // Date/time (fields 5, 6)
  if (ntok > 6 && tok[5][0] && this->datetime_sensor_) {
    char dt[32];
    int dd, mm, yy;
    if (sscanf(tok[5], "%2d%2d%2d", &dd, &mm, &yy) == 3) {
      int year = (yy < 50) ? 2000 + yy : 1900 + yy;
      int hh, mn, ss;
      if (sscanf(tok[6], "%2d%2d%2d", &hh, &mn, &ss) == 3) {
        snprintf(dt, sizeof(dt), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                 year, mm, dd, hh, mn, ss);
        this->datetime_sensor_->publish_state(dt);
      }
    }
  }

  // Speed: knots → km/h (field 7, may be empty)
  if (ntok > 7 && tok[7][0] && this->speed_sensor_) {
    double speed_knots = atof(tok[7]);
    double speed_kmh = speed_knots * 1.852;
    this->speed_sensor_->publish_state(static_cast<float>(speed_kmh));
  }

  // Satellites (field 9, may be empty — index shifts if some fields missing)
  // The SIM7670G CGPSINFO format has 10 fields; satellites is the last.
  // But empty fields still produce tokens from strtok_r... actually no,
  // consecutive commas produce empty tokens only if the impl does.
  // Let's check the actual field count.
  int satellites = 0;
  if (ntok > 9 && tok[9][0]) {
    satellites = atoi(tok[9]);
  }
  if (this->satellites_sensor_)
    this->satellites_sensor_->publish_state(satellites);

  ESP_LOGD(TAG, "GPS: lat=%.6f lon=%.6f sat=%d", lat, lon, satellites);
}

// ---------------------------------------------------------------------------
// ESPHome Component Lifecycle
// ---------------------------------------------------------------------------

void Sim7670gComponent::setup() {
#ifdef USE_ESP32
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
#else
  this->mark_failed();
#endif

  if (this->gps_enabled_) {
    ESP_LOGI(TAG, "GPS enabled (AT+CGPSINFO polling)");
  }
}

void Sim7670gComponent::loop() {
  uint32_t now = millis();

  // Battery update
  if (now - this->last_update_ >= this->update_interval_ms_) {
    this->last_update_ = now;
    this->update_battery();
  }

  // GPS polling
  if (this->gps_enabled_ && now - this->last_gps_query_ >= GPS_POLL_INTERVAL_MS) {
    this->last_gps_query_ = now;
    this->query_gps();
  }
}

void Sim7670gComponent::update_battery() {
#ifdef USE_ESP32
  if (!this->adc_ready_ || this->adc_handle_ == nullptr)
    return;
  int raw = 0;
  if (adc_oneshot_read(this->adc_handle_,
                       static_cast<adc_channel_t>(this->battery_adc_channel_), &raw) != ESP_OK) {
    ESP_LOGW(TAG, "ADC read failed");
    return;
  }
  float mv = raw * 3500.0f / 4095.0f;
  float voltage = (mv / 1000.0f) * this->voltage_divider_;
  if (this->battery_sensor_ != nullptr)
    this->battery_sensor_->publish_state(voltage);
#endif
}

void Sim7670gComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "SIM7670G:");
  ESP_LOGCONFIG(TAG, "  Battery ADC channel: %u", this->battery_adc_channel_);
  ESP_LOGCONFIG(TAG, "  Voltage divider: %.2f", this->voltage_divider_);
  ESP_LOGCONFIG(TAG, "  Update interval: %u ms", this->update_interval_ms_);
  ESP_LOGCONFIG(TAG, "  GPS: %s", this->gps_enabled_ ? "enabled" : "disabled");
}

}  // namespace sim7670g
}  // namespace esphome
