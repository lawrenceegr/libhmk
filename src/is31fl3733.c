/*
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "is31fl3733.h"

#if defined(RGB_ENABLED) && defined(RGB_DRIVER_IS31FL3733)

#include "hardware/i2c_api.h"

// Command register: page select, writable after the unlock below
#define REG_COMMAND 0xFD
// Command register write lock: write 0xC5 to allow one write to REG_COMMAND
#define REG_COMMAND_WRITE_LOCK 0xFE
#define COMMAND_WRITE_UNLOCK 0xC5

#define PAGE_LED_CONTROL 0x00
#define PAGE_PWM 0x01
#define PAGE_AUTO_BREATH 0x02
#define PAGE_FUNCTION 0x03

// Page 0: 24 on/off bytes, two per SW row (CS1-8, CS9-16)
#define LED_CONTROL_SIZE 24
// Page 3 registers
#define REG_CONFIGURATION 0x00
#define REG_GLOBAL_CURRENT 0x01
// Configuration: SSD = 1 (normal operation), PWM mode, no sync
#define CONFIGURATION_NORMAL 0x01

static const uint8_t addresses[] = RGB_DRIVER_ADDRESSES;

// Register address (0x00) followed by the 192 PWM values, sent in one transfer
static uint8_t frame[RGB_NUM_DRIVERS][1 + IS31FL3733_NUM_CHANNELS];
static bool frame_dirty[RGB_NUM_DRIVERS];
static bool frame_sending[RGB_NUM_DRIVERS];

static bool write_reg(uint8_t drv, uint8_t reg, uint8_t value) {
  const uint8_t buf[2] = {reg, value};
  return i2c_bus_write(drv, addresses[drv], buf, sizeof(buf));
}

static bool select_page(uint8_t drv, uint8_t page) {
  return write_reg(drv, REG_COMMAND_WRITE_LOCK, COMMAND_WRITE_UNLOCK) &&
         write_reg(drv, REG_COMMAND, page);
}

// Write `len` copies of `value` starting at register 0 of the current page
static bool fill_page(uint8_t drv, uint8_t value, uint8_t len) {
  uint8_t buf[1 + IS31FL3733_NUM_CHANNELS];

  buf[0] = 0x00;
  memset(&buf[1], value, len);
  return i2c_bus_write(drv, addresses[drv], buf, (uint8_t)(len + 1));
}

bool is31fl3733_init(uint8_t drv) {
  i2c_bus_init(drv);

  memset(frame[drv], 0, sizeof(frame[drv]));
  frame_dirty[drv] = false;
  frame_sending[drv] = false;

  bool ok = true;
  // Shut down while configuring, then normal operation with the global current
  ok = ok && select_page(drv, PAGE_FUNCTION) &&
       write_reg(drv, REG_CONFIGURATION, 0x00) &&
       write_reg(drv, REG_GLOBAL_CURRENT, RGB_GLOBAL_CURRENT);
  // Every channel in plain PWM mode (not auto breath)
  ok = ok && select_page(drv, PAGE_AUTO_BREATH) &&
       fill_page(drv, 0x00, IS31FL3733_NUM_CHANNELS);
  // Every channel off (PWM 0), then enabled
  ok = ok && select_page(drv, PAGE_PWM) &&
       fill_page(drv, 0x00, IS31FL3733_NUM_CHANNELS);
  ok = ok && select_page(drv, PAGE_LED_CONTROL) &&
       fill_page(drv, 0xFF, LED_CONTROL_SIZE);
  ok = ok && select_page(drv, PAGE_FUNCTION) &&
       write_reg(drv, REG_CONFIGURATION, CONFIGURATION_NORMAL);
  // Stay on the PWM page: every frame is then a single write from register 0
  ok = ok && select_page(drv, PAGE_PWM);

  return ok;
}

void is31fl3733_set_pwm(uint8_t drv, uint8_t channel, uint8_t value) {
  if (drv >= RGB_NUM_DRIVERS || channel >= IS31FL3733_NUM_CHANNELS)
    return;

  uint8_t *p = &frame[drv][1 + channel];
  if (*p != value) {
    *p = value;
    frame_dirty[drv] = true;
  }
}

bool is31fl3733_busy(uint8_t drv) {
  if (!frame_sending[drv])
    return false;

  const i2c_bus_status_t status = i2c_bus_poll(drv);
  if (status == I2C_BUS_BUSY)
    return true;

  frame_sending[drv] = false;
  if (status == I2C_BUS_ERROR)
    // Try the frame again next time
    frame_dirty[drv] = true;
  return false;
}

void is31fl3733_flush(uint8_t drv) {
  if (!frame_dirty[drv] || is31fl3733_busy(drv))
    return;

  frame[drv][0] = 0x00;
  if (i2c_bus_write_async(drv, addresses[drv], frame[drv],
                          (uint8_t)sizeof(frame[drv]))) {
    frame_dirty[drv] = false;
    frame_sending[drv] = true;
  }
}

#endif
