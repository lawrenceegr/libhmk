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

#pragma once

#include "common.h"

//--------------------------------------------------------------------+
// IS31FL3733 12x16 LED matrix driver
//--------------------------------------------------------------------+

// PWM registers: (SW - 1) * 16 + (CS - 1)
#define IS31FL3733_NUM_CHANNELS 192

/**
 * @brief Initialize one driver chip (blocking, call once at startup)
 *
 * Turns every channel on with PWM 0, sets the global current and leaves the
 * chip on the PWM page so frames can be streamed with `is31fl3733_flush()`.
 *
 * @param drv Driver index (also its I2C bus index)
 *
 * @return true if the chip acknowledged every write
 */
bool is31fl3733_init(uint8_t drv);

/**
 * @brief Set one channel's PWM value in the frame buffer
 *
 * @param drv Driver index
 * @param channel PWM register index (0-191)
 * @param value PWM duty (0-255)
 *
 * @return None
 */
void is31fl3733_set_pwm(uint8_t drv, uint8_t channel, uint8_t value);

/**
 * @brief Start sending the frame buffer if it changed and the bus is free
 *
 * @param drv Driver index
 *
 * @return None
 */
void is31fl3733_flush(uint8_t drv);

/**
 * @brief Whether a frame is still being sent
 *
 * Also collects the result of the last transfer: a failed frame is marked to
 * be sent again.
 *
 * @param drv Driver index
 *
 * @return true while the I2C transfer is in progress
 */
bool is31fl3733_busy(uint8_t drv);
