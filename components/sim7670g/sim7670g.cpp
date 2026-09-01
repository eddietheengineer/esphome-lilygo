#include "sim7670g.h"

#include "esphome/core/log.h"


namespace esphome {
namespace sim7670g {

static const char *const TAG = "sim7670g";

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
      .atten = ADC_ATTEN_DB_11,
      .bitwidth = ADC_BITWIDTH_12,
  };
  if (adc_oneshot_config_channel(this->adc_handle_,
                                  static_cast<adc_channel_t>(this->battery_adc_channel_),
                                  &chan_config) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure ADC channel %u", this->battery_adc_channel_);
    this->mark_failed();
  }
  // Create ADC calibration scheme (curve fitting) for accurate mV conversion.
  // adc_cali reads the actual reference voltage from eFuse, fixing the scaling
  // that a hardcoded 1100mV reference would get wrong (varies 1000-1200mV per chip).
  adc_cali_curve_fitting_config_t cali_cfg = {
      .unit_id = ADC_UNIT_1,
      .atten = ADC_ATTEN_DB_11,
  };
  if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &this->adc_cali_handle_) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create ADC calibration scheme");
    this->mark_failed();
    return;
  }
  this->adc_ready_ = true;
  ESP_LOGI(TAG, "Battery ADC ready on channel %u (divider %.2f)", this->battery_adc_channel_,
           this->voltage_divider_);
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
  // adc_cali_raw_to_voltage converts raw ADC to calibrated mV using eFuse reference.
  int v_pin_mv = 0;
  if (adc_cali_raw_to_voltage(this->adc_cali_handle_, adc_val, &v_pin_mv) != ESP_OK) {
    ESP_LOGW(TAG, "ADC calibration failed, using raw value");
    return;
  }
  float v_battery = (v_pin_mv * this->voltage_divider_) / 1000.0f;
  ESP_LOGI(TAG, "Battery: raw=%d pin=%dmV bat=%.2fV", adc_val, v_pin_mv, v_battery);
  this->battery_sensor_->publish_state(v_battery);
#endif
}
void Sim7670gComponent::dump_config() {
  LOG_SENSOR("  ", "Battery Voltage", this->battery_sensor_);
}

}  // namespace sim7670g
}  // namespace esphome
