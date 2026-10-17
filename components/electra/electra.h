#pragma once

#include "esphome/components/climate_ir/climate_ir.h"
#include "esphome/core/automation.h"

namespace esphome {
namespace electra {

// Temperature range supported by Electra RC-3 / RC-7 remotes
const float RC3_TEMP_MIN = 16.0;
const float RC3_TEMP_MAX = 30.0;

typedef union ElectraCode {
  // That configuration has a total of 34 bits
  //    33: Power bit, if this bit is ON, the A/C will toggle its power.
  // 32-30: Mode - Cool, Heat, Auto, Dry, Fan
  // 29-28: Fan - Low, Medium, High, Auto
  //    27: iFeel telemetry bit: 1 if this is a remote ambient sensor broadcast
  //    26: Zero
  //    25: Swing On/Off
  //    24: iFeel On/Off
  //    23: iFeel temp bit
  // 22-19: Temperature (encoded as target_temp - 15)
  //    18: Sleep mode On/Off
  // 17- 2: Zeros
  //     1: One
  //     0: Zero
  uint64_t num;
  struct {
    uint64_t zeros1 : 1;
    uint64_t ones1 : 1;
    uint64_t zeros2 : 16;
    uint64_t sleep : 1;
    uint64_t temperature : 4;
    uint64_t ifeel_temp : 1;
    uint64_t ifeel : 1;
    uint64_t swing : 1;
    uint64_t zeros3 : 1;
    uint64_t ifeel_oriented : 1;
    uint64_t fan : 2;
    uint64_t mode : 3;
    uint64_t power : 1;
  };
} ElectraCode;

typedef enum IRElectraFan {
  IRElectraFanLow = 0b00,
  IRElectraFanMedium = 0b01,
  IRElectraFanHigh = 0b10,
  IRElectraFanAuto = 0b11
} IRElectraFan;

typedef enum IRElectraMode {
  IRElectraModeCool = 0b001,
  IRElectraModeHeat = 0b010,
  IRElectraModeAuto = 0b011,
  IRElectraModeDry = 0b100,
  IRElectraModeFan = 0b101,
  IRElectraModeOff = 0b111
} IRElectraMode;

class ElectraClimate : public climate_ir::ClimateIR {
 public:
  ElectraClimate()
      : climate_ir::ClimateIR(RC3_TEMP_MIN, RC3_TEMP_MAX, 1.0f, true, true,
                              {climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW, climate::CLIMATE_FAN_MEDIUM,
                               climate::CLIMATE_FAN_HIGH},
                              {climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL},
                              {climate::CLIMATE_PRESET_NONE, climate::CLIMATE_PRESET_SLEEP}) {}

  void setup() override;
  void dump_config() override;
  void set_supports_off_command(bool supports) { this->supports_off_ = supports; }
  void setOffSupport(bool supports) { this->set_supports_off_command(supports); }  // Backwards compatibility
  void sync_state();

 protected:
  static const int32_t ELECTRA_TIME_UNIT = 1000;
  static const int32_t ELECTRA_NUM_BITS = 34;

  bool supports_off_{true};
  uint32_t last_transmit_time_{0};
  climate::ClimateMode active_mode_{climate::CLIMATE_MODE_OFF};
  climate::ClimateMode last_on_mode_{climate::CLIMATE_MODE_COOL};

  /// Transmit via IR the state of this climate controller.
  void transmit_state() override;
  /// Encode internal climate state into 34-bit Electra packet
  ElectraCode encode_electra();
  /// Handle received IR buffer
  bool on_receive(remote_base::RemoteReceiveData data) override;
  /// Override control
  void control(const climate::ClimateCall &call) override;
  /// Decode incoming signal pulses
  ElectraCode decode_electra(remote_base::RemoteReceiveData data);
  ElectraCode analyze_electra(remote_base::RemoteReceiveData &data);
};

template<typename... Ts> class SyncStateAction : public Action<Ts...> {
 public:
  SyncStateAction(ElectraClimate *parent) : parent_(parent) {}
  void play(Ts...) override { this->parent_->sync_state(); }

 protected:
  ElectraClimate *parent_;
};

}  // namespace electra
}  // namespace esphome
