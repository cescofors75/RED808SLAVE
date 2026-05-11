#pragma once

#include <stdint.h>

namespace devices {

enum ControlId : uint8_t {
  CTRL_DF_ROTARY_0 = 0,
  CTRL_DF_ROTARY_1 = 1,
  CTRL_DF_ROTARY_2 = 2,
  CTRL_DF_ROTARY_3 = 3,
  CTRL_M5_ENC_BANK_0 = 4,
  CTRL_M5_ENC_BANK_1 = 5,
  CTRL_BYTEBTN_0 = 6,
  CTRL_BYTEBTN_1 = 7,
  CTRL_FADER_0 = 8,
  CTRL_I2C_HUB = 9,
};

enum HubChannel : uint8_t {
  HUB_CH_BYTEBTN_0 = 0,
  HUB_CH_BYTEBTN_1 = 1,
  HUB_CH_M5_ENC_0 = 2,
  HUB_CH_M5_ENC_1 = 3,
  HUB_CH_DF_ROT_0 = 4,
  HUB_CH_DF_ROT_1 = 5,
  HUB_CH_DF_ROT_2 = 6,
  HUB_CH_DF_ROT_3 = 7,
};

} // namespace devices
