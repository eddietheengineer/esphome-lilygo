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
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#endif

// Forward-declare microlink AT command API.
extern "C" {
  int ml_cellular_send_at(const char *cmd, char *response, size_t resp_size, int timeout_ms);
  int ml_cellular_get_state(void);
}

namespace esphome {
namespace sim7670g {

/// LILYGO T-SIM7670G-S3 component: battery ADC + GPS NMEA parser.
///
/// Battery: reads the board's battery voltage from its ADC pin.
/// GPS: reads NMEA sentences from the modem's dedicated GPS UART (UART2,
///     GPIO 45/48 on the Standard board). This is a separate physical UART
///     from the AT/PPP UART, so GPS works simultaneously with cellular data.
///     Powers on GNSS via AT+CGNSSPWR=1 on the modem's AT UART.
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

 private:
#ifdef USE_ESP32
  static void gps_rx_task(void *arg);
#endif
  void feed_nMEA(const uint8_t *data, size_t len);
  bool parse_nmea_line(const char *line);
  bool parse_gga(const char *fields_csv);
  bool parse_rmc(const char *fields_csv);
  void update_battery();

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

  bool gps_enabled_{true};
  bool gnss_powered_{false};

  // NMEA line buffer
  char nmea_buf_[128];
  size_t nmea_buf_len_{0};

#ifdef USE_ESP32
  TaskHandle_t gps_task_{nullptr};
#endif

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
