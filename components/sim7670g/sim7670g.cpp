#include "sim7670g.h"

#include "esphome/core/log.h"

#ifdef USE_ESP32
#include "esp_adc/adc_oneshot.h"
#endif

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
  // 12 dB attenuation + 12-bit: widest range (~0-3.5 V) for a 3.7-4.2 V cell
  // behind a divider.
  adc_oneshot_chan_cfg_t chan_config = {
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_12,
  };
  if (adc_oneshot_config_channel(this->adc_handle_, static_cast<adc_channel_t>(this->battery_adc_channel_),
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
}

void Sim7670gComponent::loop() {
  uint32_t now = millis();
  if (now - this->last_update_ < this->update_interval_ms_)
    return;
  this->last_update_ = now;
  this->update_battery();
}

void Sim7670gComponent::update_battery() {
#ifdef USE_ESP32
  if (!this->adc_ready_ || this->adc_handle_ == nullptr)
    return;
  int raw = 0;
  if (adc_oneshot_read(this->adc_handle_, static_cast<adc_channel_t>(this->battery_adc_channel_), &raw) != ESP_OK) {
    ESP_LOGW(TAG, "ADC read failed");
    return;
  }
  // ESP32-S3 ADC1 with 11 dB attenuation spans 0-3500 mV over 12 bits (0-4095).
  float mv = raw * 3500.0f / 4095.0f;
  // The board divides the battery voltage down (2:1 on the T-SIM7670G-S3); restore it.
  float voltage = (mv / 1000.0f) * this->voltage_divider_;
  if (this->battery_sensor_ != nullptr)
    this->battery_sensor_->publish_state(voltage);
#endif
}

void Sim7670gComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "SIM7670G Battery Sensor:");
  ESP_LOGCONFIG(TAG, "  Battery ADC channel: %u", this->battery_adc_channel_);
  ESP_LOGCONFIG(TAG, "  Voltage divider: %.2f", this->voltage_divider_);
  ESP_LOGCONFIG(TAG, "  Update interval: %u ms", this->update_interval_ms_);
}

}  // namespace sim7670g
}  // namespace esphome
