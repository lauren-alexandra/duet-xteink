#include "HalPowerRails.h"

#include <Arduino.h>
#include <BoardConfig.h>
#include <Logging.h>
#include <PowerManager.h>
#include <driver/gpio.h>

namespace HalPowerRails {

void prepareForDeepSleep() {
  freeink::PowerManager::powerDownRailsForSleep();

  for (const int8_t pin : {BoardConfig::ACTIVE.power.latch0, BoardConfig::ACTIVE.power.latch1}) {
    if (pin < 0) {
      continue;
    }
    if (BoardConfig::latchConflictsWithBus(pin)) {
      LOG_ERR("PWR", "Refusing to lower power latch on bus pin %d", static_cast<int>(pin));
      continue;
    }

    const auto gpioPin = static_cast<gpio_num_t>(pin);
    gpio_hold_dis(gpioPin);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    gpio_hold_en(gpioPin);
  }
}

}  // namespace HalPowerRails
