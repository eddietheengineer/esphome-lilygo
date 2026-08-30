#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#ifdef USE_ESP32
#include "esp_adc/adc_oneshot.h"
#endif

// Forward-declare the microlink RX callback type so we can register without
// including the full ml_cellular.h (keeps the component self-contained).
typedef void (*ml_rx_callback_t)(const uint8_t *data, size_t len, void *arg);
extern "C" {
  void ml_cellular_set_rx_callback(ml_rx_callback_t cb, void *arg);
}

namespace esphome {
namespace sim7670g {

/// LILYGO T-SIM7670G-S3 component: battery ADC + GPS NMEA parser.
///
/// Battery: reads the board's battery voltage from its ADC pin.
/// GPS: registers an RX byte callback with the microlink cellular driver to
///     intercept NMEA sentences from the SIM7670G modem UART during AT command
///     phases (before PPP dials and in AT socket fallback mode). Parses GGA
///     and RMC sentences to expose latitude, longitude, altitude, speed,
///     satellites, HDOP, datetime, and fix status.
class Sim7670gComponent : public Component {
 public:
  // Battery
  void set_battery_adc_channel(uint8_t channel) { this->battery_adc_channel_ = channel; }
  void set_voltage_divider(float divider) { this->voltage_divider_ = divider; }
  void set_update_interval(uint32_t ms) { this->update_interval_ms_ = ms; }
  void set_battery_sensor(sensor::Sensor *s) { this->battery_sensor_ = s; }

  // GPS
  void set_gps_enabled(bool enabled) { this->gps_enabled_ = enabled; }

  void set_latitude_sensor(sensor::Sensor *s) { this->latitude_sensor_ = s; }
  void set_longitude_sensor(sensor::Sensor *s) { this->longitude_sensor_ = s; }
  void set_altitude_sensor(sensor::Sensor *s) { this->altitude_sensor_ = s; }
  void set_speed_sensor(sensor::Sensor *s) { this->speed_sensor_ = s; }
  void set_satellites_sensor(sensor::Sensor *s) { this->satellites_sensor_ = s; }
  void set_hdop_sensor(sensor::Sensor *s) { this->hdop_sensor_ = s; }

  void set_datetime_sensor(text_sensor::TextSensor *s) { this->datetime_sensor_ = s; }
  void set_fix_status_sensor(text_sensor::TextSensor *s) { this->fix_status_sensor_ = s; }

  float get_setup_priority() const override { return setup_priority::DATA; }
  void setup() override;
  void loop() override;
  void dump_config() override;

  /// Feed a chunk of UART RX bytes into the NMEA parser.
  void feed_nMEA(const uint8_t *data, size_t len);

 private:
  static void rx_callback(const uint8_t *data, size_t len, void *arg);

  void update_battery();
  bool parse_nmea_line(const char *line);
  bool parse_gga(const char *line);
  bool parse_rmc(const char *line);

  // Battery
  uint8_t battery_adc_channel_{8};
  float voltage_divider_{2.0f};
  uint32_t update_interval_ms_{30000};
  uint32_t last_update_{0};

#ifdef USE_ESP32
  bool adc_ready_{false};
  adc_oneshot_unit_handle_t adc_handle_{nullptr};
#endif
  sensor::Sensor *battery_sensor_{nullptr};

  // GPS
  bool gps_enabled_{true};

  // NMEA line buffer (max NMEA sentence ~83 chars + margin)
  char nmea_buf_[128];
  size_t nmea_buf_len_{0};

  // GPS sensors
  sensor::Sensor *latitude_sensor_{nullptr};
  sensor::Sensor *longitude_sensor_{nullptr};
  sensor::Sensor *altitude_sensor_{nullptr};
  sensor::Sensor *speed_sensor_{nullptr};
  sensor::Sensor *satellites_sensor_{nullptr};
  sensor::Sensor *hdop_sensor_{nullptr};

  text_sensor::TextSensor *datetime_sensor_{nullptr};
  text_sensor::TextSensor *fix_status_sensor_{nullptr};
};

}  // namespace sim7670g
}  // namespace esphome
