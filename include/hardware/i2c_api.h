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
// I2C Master API
//--------------------------------------------------------------------+

// Buses are numbered 0..RGB_NUM_DRIVERS-1 in the order of `rgb.drivers` in
// keyboard.json. Only master writes are supported, which is all the LED
// drivers need. Addresses are 7-bit.

typedef enum {
  I2C_BUS_IDLE = 0,
  // An asynchronous write is still in progress
  I2C_BUS_BUSY,
  // The last write was not acknowledged or hit a bus error
  I2C_BUS_ERROR,
} i2c_bus_status_t;

/**
 * @brief Initialize an I2C bus (pins, peripheral at 400 kHz and its TX DMA)
 *
 * @param bus Bus index
 *
 * @return None
 */
void i2c_bus_init(uint8_t bus);

/**
 * @brief Write bytes to a device, waiting for the transfer to finish
 *
 * Only for initialization: it blocks the main loop for the whole transfer.
 *
 * @param bus Bus index
 * @param address 7-bit device address
 * @param data Bytes to write
 * @param len Number of bytes (1-255)
 *
 * @return true if the device acknowledged every byte
 */
bool i2c_bus_write(uint8_t bus, uint8_t address, const uint8_t *data,
                   uint8_t len);

/**
 * @brief Start a DMA write to a device and return immediately
 *
 * `data` must stay valid and unchanged until `i2c_bus_poll()` stops returning
 * `I2C_BUS_BUSY`.
 *
 * @param bus Bus index
 * @param address 7-bit device address
 * @param data Bytes to write
 * @param len Number of bytes (1-255)
 *
 * @return false if the bus was still busy (nothing started)
 */
bool i2c_bus_write_async(uint8_t bus, uint8_t address, const uint8_t *data,
                         uint8_t len);

/**
 * @brief Check the state of the last asynchronous write
 *
 * Reading `I2C_BUS_ERROR` clears the error, so the next call returns
 * `I2C_BUS_IDLE`.
 *
 * @param bus Bus index
 *
 * @return Bus status
 */
i2c_bus_status_t i2c_bus_poll(uint8_t bus);

/**
 * @brief Drive the LED drivers' shared shutdown pin (`rgb.sdb`), if any
 *
 * @param enable true to drive it high (drivers enabled)
 *
 * @return None
 */
void led_driver_enable(bool enable);
