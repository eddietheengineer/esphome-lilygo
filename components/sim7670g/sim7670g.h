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

// Forward-declare microlink AT command API.
extern "C" {
  int ml_cellular_send_at(const char *cmd, char *response, size_t resp_size, int timeout_ms);
  typedef enum {
      ML_CELL_STATE_OFF,
      ML_CELL_STATE_INIT,
      ML_CELL_STATE_AT_OK,
      ML_CELL_STATE_SIM_READY,
      ML_CELL_STATE_REGISTERED,
      ML_CELL_STATE_PPP_CONNECTING,
      ML_CELL_STATE_PPP_CONNECTED,
      ML_CELL_STATE_DATA_CONNECTED,
      ML_CELL_STATE_ERROR,
  } ml_cellular_state_t;
  ml_cellular_state_t ml_cellular_get_state(void);
}

namespace esphome {
namespace sim7670g {

/// LILYGO T-SIM7670G-S3 component: battery ADC + GPS via AT+CGPSINFO polling.
///
/// Battery: reads the board's battery voltage from its ADC pin.
/// GPS: powers on the SIM7670G GNSS engine (AT+CGNSSPWR=1) during setup,
///     then periodically polls AT+CGPSINFO to get the last fix. Polling only
///     works when the modem is NOT in PPP mode (AT command phase or AT socket
///     fallback). During PPP mode the UART carries PPP data — GPS is unavailable.
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
  void update_battery();
  void query_gps();

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
  bool gnss_powered_{false};
  uint32_t last_gps_query_{0};
  // Poll every 60s — AT+CGPSINFO is a blocking AT command.
  static constexpr uint32_t GPS_POLL_INTERVAL_MS = 60000;
  // Delay first query to give GNSS time to acquire after powering on.
  static constexpr uint32_t GPS_INITIAL_DELAY_MS = 30000;

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
