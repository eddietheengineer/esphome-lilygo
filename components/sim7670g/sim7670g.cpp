#include "sim7670g.h"

#include "esphome/core/log.h"

#ifdef USE_ESP32
extern "C" {
#include "ml_cellular.h"
}
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
  adc_oneshot_chan_cfg_t chan_config = {
      .atten = ADC_ATTEN_DB_11,
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

  if (this->gps_enabled_) {
    ESP_LOGW(TAG, "GPS enabled but only works in AT socket mode (not PPP/Tailscale)");
  }
}

void Sim7670gComponent::loop() {
  uint32_t now = millis();
  if (now - this->last_update_ >= this->update_interval_ms_) {
    this->last_update_ = now;
    this->update_battery();
  }

  if (this->gps_enabled_ && !this->gps_published_) {
    static uint32_t last_gps_check = 0;
    if (now - last_gps_check >= 10000) {
      last_gps_check = now;
      this->publish_gps_data();
    }
  }
}

void Sim7670gComponent::publish_gps_data() {
#ifdef USE_ESP32
  ml_cellular_info_t info;
  if (ml_cellular_get_info(&info) != ESP_OK)
    return;

  if (info.gps_latitude == 0.0 && info.gps_longitude == 0.0 && info.gps_satellites == 0)
    return;

  this->gps_published_ = true;

  if (this->latitude_sensor_)
    this->latitude_sensor_->publish_state(static_cast<float>(info.gps_latitude));
  if (this->longitude_sensor_)
    this->longitude_sensor_->publish_state(static_cast<float>(info.gps_longitude));
  if (this->altitude_sensor_)
    this->altitude_sensor_->publish_state(static_cast<float>(info.gps_altitude));
  if (this->speed_sensor_)
    this->speed_sensor_->publish_state(static_cast<float>(info.gps_speed));
  if (this->satellites_sensor_)
    this->satellites_sensor_->publish_state(info.gps_satellites);
  if (this->hdop_sensor_)
    this->hdop_sensor_->publish_state(static_cast<float>(info.gps_hdop));

  if (this->fix_status_sensor_) {
    const char *status_str = info.gps_has_fix ? "3D Fix" : "No Fix";
    this->fix_status_sensor_->publish_state(status_str);
  }

  ESP_LOGI(TAG, "GPS: lat=%.6f lon=%.6f alt=%.1f speed=%.1f sat=%d hdop=%.2f fix=%d",
           info.gps_latitude, info.gps_longitude, info.gps_altitude,
           info.gps_speed, info.gps_satellites, info.gps_hdop, (int)info.gps_has_fix);
#endif
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

  // LilyGo board: 2:1 voltage divider on battery ADC pin.
  // With ADC_ATTEN_DB_11, the ADC reference is 1100mV.
  // analogReadMilliVolts() in Arduino returns calibrated mV.
  // In ESP-IDF, adc_oneshot_read() returns raw digital value (0-4095).
  // Convert: v_pin_mv = adc_val * 1100 / 4095 (linear approx, no curve fitting).
  // Then multiply by voltage_divider_ (2.0) for actual battery voltage.
  ESP_LOGI(TAG, "Battery ADC raw=%d", adc_val);
  float v_pin_mv = adc_val * (1100.0f / 4095.0f);
  float v_battery = (v_pin_mv * this->voltage_divider_) / 1000.0f;
#endif
}
void Sim7670gComponent::dump_config() {
  LOG_SENSOR("  ", "Battery Voltage", this->battery_sensor_);
  if (this->gps_enabled_) {
    LOG_SENSOR("  ", "Latitude", this->latitude_sensor_);
    LOG_SENSOR("  ", "Longitude", this->longitude_sensor_);
    LOG_SENSOR("  ", "Altitude", this->altitude_sensor_);
    LOG_SENSOR("  ", "Speed", this->speed_sensor_);
    LOG_SENSOR("  ", "Satellites", this->satellites_sensor_);
    LOG_SENSOR("  ", "HDOP", this->hdop_sensor_);
    LOG_TEXT_SENSOR("  ", "Datetime", this->datetime_sensor_);
    LOG_TEXT_SENSOR("  ", "Fix Status", this->fix_status_sensor_);
  }
  ESP_LOGCONFIG(TAG, "  GPS: %s", this->gps_enabled_ ? "enabled" : "disabled");
}

}  // namespace sim7670g
}  // namespace esphome
