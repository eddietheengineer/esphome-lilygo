#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/components/sensor/sensor.h"

#ifdef USE_ESP32
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#endif


namespace esphome {
namespace sim7670g {

/// LILYGO T-SIM7670G-S3 component: battery + solar voltage ADC.
class Sim7670gComponent : public Component {
 public:
  void set_battery_adc_channel(uint8_t channel) { this->battery_adc_channel_ = channel; }
  void set_voltage_divider(float divider) { this->voltage_divider_ = divider; }
  void set_update_interval(uint32_t ms) { this->update_interval_ms_ = ms; }
  void set_solar_adc_channel(uint8_t channel) { this->solar_adc_channel_ = channel; }
  void set_solar_voltage_divider(float divider) { this->solar_voltage_divider_ = divider; }
  void set_solar_sensor(sensor::Sensor *s) { this->solar_sensor_ = s; }
  void set_has_solar(bool v) { this->has_solar_ = v; }

  float get_setup_priority() const override { return setup_priority::DATA; }
  void setup() override;
  void loop() override;
  void dump_config() override;

 private:
  void update_battery();
  void update_solar();

  float voltage_divider_{2.0f};
  float solar_voltage_divider_{2.0f};
  bool has_solar_{false};
  uint32_t update_interval_ms_{30000};
  uint32_t last_update_{0};

#ifdef USE_ESP32
  bool adc_ready_{false};
  adc_oneshot_unit_handle_t adc_handle_{nullptr};
  adc_cali_handle_t adc_cali_handle_{nullptr};
  uint8_t battery_adc_channel_{7};  // GPIO 8 = ADC1 channel 7 on ESP32-S3
  sensor::Sensor *battery_sensor_{nullptr};

  // Solar ADC uses ADC2 (GPIO18 = ADC2_CH7 on ESP32-S3).
  adc_oneshot_unit_handle_t solar_adc_handle_{nullptr};
  adc_cali_handle_t solar_adc_cali_handle_{nullptr};
  uint8_t solar_adc_channel_{7};  // GPIO 18 = ADC2 channel 7 on ESP32-S3
  sensor::Sensor *solar_sensor_{nullptr};
#endif
};

}  // namespace sim7670g
}  // namespace esphome
