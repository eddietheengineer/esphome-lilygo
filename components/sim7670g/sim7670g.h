#pragma once

#include <cstdint>
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/components/sensor/sensor.h"

#ifdef USE_ESP32
#include "esp_adc/adc_oneshot.h"
#endif

namespace esphome {
namespace sim7670g {

/// Reads the LILYGO T-SIM7670G-S3 battery voltage from its ADC pin.
///
/// The cellular data path is handled by the microlink (Tailscale) component's
/// built-in SIM7670G PPP driver, so this component only exposes the board's
/// battery voltage (ADC1, divided 2:1 on the Standard board).
class Sim7670gComponent : public Component {
 public:
  void set_battery_adc_channel(uint8_t channel) { this->battery_adc_channel_ = channel; }
  void set_voltage_divider(float divider) { this->voltage_divider_ = divider; }
  void set_update_interval(uint32_t ms) { this->update_interval_ms_ = ms; }
  void set_battery_sensor(sensor::Sensor *s) { this->battery_sensor_ = s; }

  float get_setup_priority() const override { return setup_priority::DATA; }
  void setup() override;
  void loop() override;
  void dump_config() override;

 private:
  void update_battery();

  uint8_t battery_adc_channel_{8};
  float voltage_divider_{2.0f};
  uint32_t update_interval_ms_{30000};
  uint32_t last_update_{0};

#ifdef USE_ESP32
  bool adc_ready_{false};
  adc_oneshot_unit_handle_t adc_handle_{nullptr};
#endif
  sensor::Sensor *battery_sensor_{nullptr};
};

}  // namespace sim7670g
}  // namespace esphome
