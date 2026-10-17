#include "electra.h"
#include "esphome/core/log.h"

namespace esphome {
namespace electra {

static const char *const TAG = "electra.climate";

void ElectraClimate::setup() {
  climate_ir::ClimateIR::setup();
  this->active_mode_ = this->mode;
  if (this->mode != climate::CLIMATE_MODE_OFF) {
    this->last_on_mode_ = this->mode;
  }
}

void ElectraClimate::dump_config() {
  climate_ir::ClimateIR::dump_config();
  ESP_LOGCONFIG(TAG, "Electra Climate:");
  ESP_LOGCONFIG(TAG, "  Supports Off Command: %s", YESNO(this->supports_off_));
}

void ElectraClimate::control(const climate::ClimateCall &call) {
  climate_ir::ClimateIR::control(call);
  this->active_mode_ = this->mode;
  if (this->mode != climate::CLIMATE_MODE_OFF) {
    this->last_on_mode_ = this->mode;
  }
}

void ElectraClimate::sync_state() {
  if (this->mode == climate::CLIMATE_MODE_OFF) {
    this->mode = (this->last_on_mode_ != climate::CLIMATE_MODE_OFF) ? this->last_on_mode_ : climate::CLIMATE_MODE_COOL;
  } else {
    this->last_on_mode_ = this->mode;
    this->mode = climate::CLIMATE_MODE_OFF;
  }

  this->active_mode_ = this->mode;
  this->publish_state();
}

void ElectraClimate::transmit_state() {
  if (this->active_mode_ == climate::CLIMATE_MODE_OFF && this->mode == climate::CLIMATE_MODE_OFF)
    return;

  ElectraCode code = this->encode_electra();
  this->last_transmit_time_ = millis();
  auto transmit = this->transmitter_->transmit();
  auto data = transmit.get_data();

  data->set_carrier_frequency(38000);
  uint16_t repeat = 3;

  for (uint16_t r = 0; r < repeat; r++) {
    // Header
    data->mark(3 * ELECTRA_TIME_UNIT);
    uint16_t next_value = 3 * ELECTRA_TIME_UNIT;
    bool is_next_space = true;

    // Data
    for (int j = ELECTRA_NUM_BITS - 1; j >= 0; j--) {
      uint8_t bit = (code.num >> j) & 1;

      // If current index is SPACE
      if (is_next_space) {
        if (bit == 1) {
          data->space(next_value + ELECTRA_TIME_UNIT);
          next_value = ELECTRA_TIME_UNIT;
          is_next_space = false;
        } else {
          data->space(next_value);
          data->mark(ELECTRA_TIME_UNIT);
          next_value = ELECTRA_TIME_UNIT;
          is_next_space = true;
        }
      } else {
        // Current index is MARK
        if (bit == 1) {
          data->mark(next_value);
          data->space(ELECTRA_TIME_UNIT);
          next_value = ELECTRA_TIME_UNIT;
          is_next_space = false;
        } else {
          data->mark(next_value + ELECTRA_TIME_UNIT);
          next_value = ELECTRA_TIME_UNIT;
          is_next_space = true;
        }
      }
    }

    // Last value must be SPACE
    data->space(next_value);
  }

  // Footer
  data->mark(4 * ELECTRA_TIME_UNIT);

  transmit.perform();
}

ElectraCode ElectraClimate::encode_electra() {
  ElectraCode code = {0};
  code.ones1 = 1;

  // Set swing
  if (this->swing_mode == climate::CLIMATE_SWING_VERTICAL) {
    code.swing = 1;
  } else {
    code.swing = 0;
  }

  // Set sleep preset
  if (this->preset == climate::CLIMATE_PRESET_SLEEP) {
    code.sleep = 1;
  } else {
    code.sleep = 0;
  }

  // Set fan mode
  switch (this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO)) {
    case climate::CLIMATE_FAN_LOW:
      code.fan = IRElectraFan::IRElectraFanLow;
      break;
    case climate::CLIMATE_FAN_MEDIUM:
      code.fan = IRElectraFan::IRElectraFanMedium;
      break;
    case climate::CLIMATE_FAN_HIGH:
      code.fan = IRElectraFan::IRElectraFanHigh;
      break;
    case climate::CLIMATE_FAN_AUTO:
    default:
      code.fan = IRElectraFan::IRElectraFanAuto;
      break;
  }

  // Set climate mode & power bit
  switch (this->mode) {
    case climate::CLIMATE_MODE_COOL:
      code.mode = IRElectraMode::IRElectraModeCool;
      code.power = (this->active_mode_ == climate::CLIMATE_MODE_OFF) ? 1 : 0;
      break;
    case climate::CLIMATE_MODE_HEAT:
      code.mode = IRElectraMode::IRElectraModeHeat;
      code.power = (this->active_mode_ == climate::CLIMATE_MODE_OFF) ? 1 : 0;
      break;
    case climate::CLIMATE_MODE_FAN_ONLY:
      code.mode = IRElectraMode::IRElectraModeFan;
      code.power = (this->active_mode_ == climate::CLIMATE_MODE_OFF) ? 1 : 0;
      break;
    case climate::CLIMATE_MODE_HEAT_COOL:
      code.mode = IRElectraMode::IRElectraModeAuto;
      code.power = (this->active_mode_ == climate::CLIMATE_MODE_OFF) ? 1 : 0;
      break;
    case climate::CLIMATE_MODE_DRY:
      code.mode = IRElectraMode::IRElectraModeDry;
      code.power = (this->active_mode_ == climate::CLIMATE_MODE_OFF) ? 1 : 0;
      break;
    case climate::CLIMATE_MODE_OFF:
    default:
      if (this->supports_off_) {
        code.mode = IRElectraMode::IRElectraModeOff;
        code.power = 1;
      } else {
        code.mode = IRElectraMode::IRElectraModeCool;
        code.power = 1;
      }
      break;
  }

  // Set temperature
  auto temp = std::lround(clamp(this->target_temperature, this->minimum_temperature_, this->maximum_temperature_));
  code.temperature = temp - 15;

  ESP_LOGD(TAG, "Encoded Electra code: 0x%09llX (%llu)", (unsigned long long) code.num,
           (unsigned long long) code.num);
  return code;
}

bool ElectraClimate::on_receive(remote_base::RemoteReceiveData data) {
  if (millis() - this->last_transmit_time_ < 500) {
    ESP_LOGV(TAG, "Blocked receive because of recent transmission");
    return false;
  }
  data.set_tolerance((ELECTRA_TIME_UNIT / 2), esphome::remote_base::TOLERANCE_MODE_TIME);
  ElectraCode decode = analyze_electra(data);

  if (decode.num == 0)
    return false;

  // Ignore periodic iFeel telemetry beacons so they do not overwrite user setpoints
  if (decode.ifeel_oriented == 1) {
    ESP_LOGD(TAG, "Received Electra iFeel beacon (room temp: %u°C)", (unsigned) (decode.temperature + 15));
    return true;
  }

  // Check if this is an explicit OFF or power-off toggle
  if (decode.mode == IRElectraMode::IRElectraModeOff ||
      (decode.power == 1 && this->mode != climate::CLIMATE_MODE_OFF)) {
    this->mode = climate::CLIMATE_MODE_OFF;
  } else if (decode.power == 1 && this->mode == climate::CLIMATE_MODE_OFF) {
    // Power turned ON from physical remote
    switch (decode.mode) {
      case IRElectraMode::IRElectraModeCool:
        this->mode = climate::CLIMATE_MODE_COOL;
        break;
      case IRElectraMode::IRElectraModeHeat:
        this->mode = climate::CLIMATE_MODE_HEAT;
        break;
      case IRElectraMode::IRElectraModeFan:
        this->mode = climate::CLIMATE_MODE_FAN_ONLY;
        break;
      case IRElectraMode::IRElectraModeAuto:
        this->mode = climate::CLIMATE_MODE_HEAT_COOL;
        break;
      case IRElectraMode::IRElectraModeDry:
        this->mode = climate::CLIMATE_MODE_DRY;
        break;
      default:
        this->mode = climate::CLIMATE_MODE_COOL;
        break;
    }
  } else if (decode.power == 0 && this->mode != climate::CLIMATE_MODE_OFF) {
    // Mode changed while AC is on
    switch (decode.mode) {
      case IRElectraMode::IRElectraModeCool:
        this->mode = climate::CLIMATE_MODE_COOL;
        break;
      case IRElectraMode::IRElectraModeHeat:
        this->mode = climate::CLIMATE_MODE_HEAT;
        break;
      case IRElectraMode::IRElectraModeFan:
        this->mode = climate::CLIMATE_MODE_FAN_ONLY;
        break;
      case IRElectraMode::IRElectraModeAuto:
        this->mode = climate::CLIMATE_MODE_HEAT_COOL;
        break;
      case IRElectraMode::IRElectraModeDry:
        this->mode = climate::CLIMATE_MODE_DRY;
        break;
      default:
        break;
    }
  } else {
    ESP_LOGD(TAG, "Mode change received while AC is off");
  }

  // Fan mode
  switch (decode.fan) {
    case IRElectraFan::IRElectraFanLow:
      this->fan_mode = climate::CLIMATE_FAN_LOW;
      break;
    case IRElectraFan::IRElectraFanMedium:
      this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
      break;
    case IRElectraFan::IRElectraFanHigh:
      this->fan_mode = climate::CLIMATE_FAN_HIGH;
      break;
    case IRElectraFan::IRElectraFanAuto:
      this->fan_mode = climate::CLIMATE_FAN_AUTO;
      break;
  }

  // Swing mode
  if (decode.swing == 1) {
    this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
  } else {
    this->swing_mode = climate::CLIMATE_SWING_OFF;
  }

  // Preset
  if (decode.sleep == 1) {
    this->preset = climate::CLIMATE_PRESET_SLEEP;
  } else {
    this->preset = climate::CLIMATE_PRESET_NONE;
  }

  // Target temperature
  this->target_temperature = (decode.temperature + 15);

  if (this->mode != climate::CLIMATE_MODE_OFF) {
    this->last_on_mode_ = this->mode;
  }
  this->active_mode_ = this->mode;
  this->publish_state();
  return true;
}

ElectraCode ElectraClimate::decode_electra(remote_base::RemoteReceiveData data) {
  const size_t total_bits = ELECTRA_NUM_BITS * 2;
  // Safety margin prevents out-of-bounds writes on edge conditions
  bool bits[total_bits + 4]{};
  ElectraCode decode = {0};
  bool longheader = false;

  if (data.expect_space(ELECTRA_TIME_UNIT * 4)) {
    longheader = true;
  } else {
    data.advance();
  }

  for (size_t i = 0; i < total_bits;) {
    if (longheader && data.peek_mark(ELECTRA_TIME_UNIT * 2)) {
      longheader = false;
      bits[i] = true;
      bits[i + 1] = false;
      bits[i + 2] = false;
      i += 3;
    } else if (longheader && (data.peek_mark(ELECTRA_TIME_UNIT) || data.peek_mark(ELECTRA_TIME_UNIT - 400))) {
      longheader = false;
      bits[i] = true;
      bits[i + 1] = false;
      i += 2;
    } else if (data.peek_mark(ELECTRA_TIME_UNIT) || data.peek_mark(ELECTRA_TIME_UNIT - 400)) {
      bits[i] = false;
      i++;
    } else if (data.peek_space(ELECTRA_TIME_UNIT) || data.peek_space(ELECTRA_TIME_UNIT - 400)) {
      bits[i] = true;
      i++;
    } else if (data.peek_mark(ELECTRA_TIME_UNIT * 2)) {
      bits[i] = false;
      bits[i + 1] = false;
      i += 2;
    } else if (data.peek_space(ELECTRA_TIME_UNIT * 2)) {
      bits[i] = true;
      bits[i + 1] = true;
      i += 2;
    } else {
      ESP_LOGV(TAG, "Bit duration error at bit %zu", i);
      return {0};
    }
    data.advance();
  }

  for (size_t i = 0; i < ELECTRA_NUM_BITS; i++) {
    if (bits[i * 2] && !bits[i * 2 + 1]) {
      decode.num |= (1ULL << ((ELECTRA_NUM_BITS - 1) - i));
    } else if (!bits[i * 2] && bits[i * 2 + 1]) {
      decode.num &= ~(1ULL << ((ELECTRA_NUM_BITS - 1) - i));
    } else {
      ESP_LOGV(TAG, "Not Manchester coded at bit %zu", i);
      return {0};
    }
  }

  ESP_LOGD(TAG, "Decoded Electra code: 0x%09llX (%llu)", (unsigned long long) decode.num,
           (unsigned long long) decode.num);

  if ((decode.ones1 != 1) || (decode.zeros1 != 0) || (decode.zeros2 != 0) || (decode.zeros3 != 0)) {
    ESP_LOGV(TAG, "Decoded successfully, but structure corrupted");
    return {0};
  }

  return decode;
}

ElectraCode ElectraClimate::analyze_electra(remote_base::RemoteReceiveData &data) {
  ElectraCode decode = {0};
  int attempts = 0;
  int runs;
  while (attempts < 3) {
    for (runs = 0;
         (!data.peek_space(ELECTRA_TIME_UNIT * 3) && !data.peek_space(ELECTRA_TIME_UNIT * 4)) &&
         runs < (3 * ELECTRA_NUM_BITS);
         runs++) {
      data.advance();
    }
    if (runs >= (3 * ELECTRA_NUM_BITS)) {
      ESP_LOGV(TAG, "No headers found");
      return {0};
    }
    decode = decode_electra(data);
    if (decode.num != 0) {
      return decode;
    }
    ESP_LOGV(TAG, "Failed to decode, trying again");
    data.advance();
    attempts++;
  }
  ESP_LOGV(TAG, "Decoding failed 3 times, stops trying");
  return {0};
}

}  // namespace electra
}  // namespace esphome
