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

#include "hardware/hardware.h"
#include "hardware/i2c_api.h"

#if defined(RGB_ENABLED)

#include "at32f402_405.h"

// I2C_CLKCTRL for 400 kHz with PCLK1 = 108 MHz (see `board_clock_init`):
// DIV = 2 (36 MHz, 27.8 ns), SCLL = 49 (1.39 us), SCLH = 29 (0.83 us),
// SCLD = 4 (139 ns data setup), SDAD = 2 (83 ns data hold).
#define I2C_CLKCTRL_400K 0x20421D31
// Longest transfer is 255 bytes, about 6 ms at 400 kHz
#define I2C_TIMEOUT_MS 20

static const uint8_t bus_numbers[] = RGB_I2C_BUSES;
static gpio_type *const sda_ports[] = RGB_I2C_SDA_PORTS;
static const uint8_t sda_pins[] = RGB_I2C_SDA_PINS;
static gpio_type *const scl_ports[] = RGB_I2C_SCL_PORTS;
static const uint8_t scl_pins[] = RGB_I2C_SCL_PINS;
static const uint8_t pin_mux[] = RGB_I2C_MUX;

_Static_assert(M_ARRAY_SIZE(bus_numbers) == RGB_NUM_DRIVERS,
               "Invalid number of I2C buses");
_Static_assert(RGB_NUM_DRIVERS <= 2, "At most two I2C buses are supported");

// DMA1 channel 1 belongs to the ADC (analog.c). Bus n uses channel n + 2.
static dma_channel_type *const dma_channels[] = {DMA1_CHANNEL2, DMA1_CHANNEL3};
static dmamux_channel_type *const dmamux_channels[] = {DMA1MUX_CHANNEL2,
                                                       DMA1MUX_CHANNEL3};

static i2c_bus_status_t bus_status[RGB_NUM_DRIVERS];
static uint32_t bus_start_time[RGB_NUM_DRIVERS];

static i2c_type *bus_i2c(uint8_t bus) {
  return bus_numbers[bus] == 1 ? I2C1 : I2C2;
}

static crm_periph_clock_type gpio_clock(const gpio_type *port) {
  if (port == GPIOA)
    return CRM_GPIOA_PERIPH_CLOCK;
  if (port == GPIOB)
    return CRM_GPIOB_PERIPH_CLOCK;
  if (port == GPIOC)
    return CRM_GPIOC_PERIPH_CLOCK;
  if (port == GPIOD)
    return CRM_GPIOD_PERIPH_CLOCK;
  return CRM_GPIOF_PERIPH_CLOCK;
}

static void pin_init(gpio_type *port, uint8_t pin, uint8_t mux) {
  gpio_init_type gpio_init_struct;

  crm_periph_clock_enable(gpio_clock(port), TRUE);
  gpio_pin_mux_config(port, (gpio_pins_source_type)pin, (gpio_mux_sel_type)mux);

  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins = (uint16_t)(1U << pin);
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  // Open drain, the board has external pull-ups
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_OPEN_DRAIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(port, &gpio_init_struct);
}

// Return the peripheral and its DMA to a clean idle state
static void bus_finish(uint8_t bus) {
  i2c_type *i2c = bus_i2c(bus);

  dma_channel_enable(dma_channels[bus], FALSE);
  i2c_dma_enable(i2c, I2C_DMA_REQUEST_TX, FALSE);
  i2c_flag_clear(i2c, I2C_STOPF_FLAG | I2C_ACKFAIL_FLAG | I2C_BUSERR_FLAG |
                          I2C_ARLOST_FLAG);
  // Flush a byte left in the transmit register after a NACK
  if (i2c_flag_get(i2c, I2C_TDBE_FLAG) == RESET)
    i2c->sts_bit.tdbe = 1;
}

// Software reset of the peripheral after a timeout. Clearing I2CEN resets the
// state machine and releases the bus; the timing registers are kept.
static void bus_reset(uint8_t bus) {
  i2c_type *i2c = bus_i2c(bus);

  bus_finish(bus);
  i2c_enable(i2c, FALSE);
  for (volatile uint32_t i = 0; i < 64; i++)
    ;
  i2c_enable(i2c, TRUE);
}

void i2c_bus_init(uint8_t bus) {
  i2c_type *i2c = bus_i2c(bus);
  dma_init_type dma_init_struct;

  crm_periph_clock_enable(bus_numbers[bus] == 1 ? CRM_I2C1_PERIPH_CLOCK
                                                : CRM_I2C2_PERIPH_CLOCK,
                          TRUE);
  crm_periph_clock_enable(CRM_DMA1_PERIPH_CLOCK, TRUE);

  pin_init(sda_ports[bus], sda_pins[bus], pin_mux[bus]);
  pin_init(scl_ports[bus], scl_pins[bus], pin_mux[bus]);

  i2c_reset(i2c);
  i2c_init(i2c, 0x0F, I2C_CLKCTRL_400K);
  i2c_enable(i2c, TRUE);

  dma_reset(dma_channels[bus]);
  dma_default_para_init(&dma_init_struct);
  dma_init_struct.buffer_size = 1;
  dma_init_struct.direction = DMA_DIR_MEMORY_TO_PERIPHERAL;
  dma_init_struct.memory_base_addr = 0;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_base_addr = (uint32_t)&i2c->txdt;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_MEDIUM;
  dma_init_struct.loop_mode_enable = FALSE;
  dma_init(dma_channels[bus], &dma_init_struct);

  dmamux_enable(DMA1, TRUE);
  dmamux_init(dmamux_channels[bus], bus_numbers[bus] == 1
                                        ? DMAMUX_DMAREQ_ID_I2C1_TX
                                        : DMAMUX_DMAREQ_ID_I2C2_TX);

  bus_status[bus] = I2C_BUS_IDLE;
}

bool i2c_bus_write_async(uint8_t bus, uint8_t address, const uint8_t *data,
                         uint8_t len) {
  i2c_type *i2c = bus_i2c(bus);

  if (len == 0 || bus_status[bus] == I2C_BUS_BUSY)
    return false;

  if (i2c_flag_get(i2c, I2C_BUSYF_FLAG) == SET)
    // Something is still holding the bus. Reset and let the caller retry.
    bus_reset(bus);

  bus_finish(bus);

  dma_channels[bus]->maddr = (uint32_t)data;
  dma_data_number_set(dma_channels[bus], len);
  dma_channel_enable(dma_channels[bus], TRUE);
  i2c_dma_enable(i2c, I2C_DMA_REQUEST_TX, TRUE);

  bus_start_time[bus] = timer_read();
  bus_status[bus] = I2C_BUS_BUSY;
  // Address, byte count and START. STOP is generated after the last byte.
  i2c_transmit_set(i2c, (uint16_t)(address << 1), len, I2C_AUTO_STOP_MODE,
                   I2C_GEN_START_WRITE);

  return true;
}

i2c_bus_status_t i2c_bus_poll(uint8_t bus) {
  i2c_type *i2c = bus_i2c(bus);

  switch (bus_status[bus]) {
  case I2C_BUS_BUSY:
    break;
  case I2C_BUS_ERROR:
    bus_status[bus] = I2C_BUS_IDLE;
    return I2C_BUS_ERROR;
  case I2C_BUS_IDLE:
  default:
    return I2C_BUS_IDLE;
  }

  const uint32_t sts = i2c->sts;
  if (sts & (I2C_ACKFAIL_FLAG | I2C_BUSERR_FLAG | I2C_ARLOST_FLAG)) {
    // A NACK also ends with an automatic STOP. Bus or arbitration errors
    // leave the peripheral in an unknown state, so reset it.
    if (sts & (I2C_BUSERR_FLAG | I2C_ARLOST_FLAG))
      bus_reset(bus);
    else
      bus_finish(bus);
    bus_status[bus] = I2C_BUS_IDLE;
    return I2C_BUS_ERROR;
  }

  if (sts & I2C_STOPF_FLAG) {
    bus_finish(bus);
    bus_status[bus] = I2C_BUS_IDLE;
    return I2C_BUS_IDLE;
  }

  if (timer_elapsed(bus_start_time[bus]) > I2C_TIMEOUT_MS) {
    bus_reset(bus);
    bus_status[bus] = I2C_BUS_IDLE;
    return I2C_BUS_ERROR;
  }

  return I2C_BUS_BUSY;
}

bool i2c_bus_write(uint8_t bus, uint8_t address, const uint8_t *data,
                   uint8_t len) {
  if (!i2c_bus_write_async(bus, address, data, len))
    return false;

  i2c_bus_status_t status;
  while ((status = i2c_bus_poll(bus)) == I2C_BUS_BUSY)
    ;

  return status == I2C_BUS_IDLE;
}

void led_driver_enable(bool enable) {
#if defined(RGB_SDB_PORT)
  static bool initialized = false;
  if (!initialized) {
    gpio_init_type gpio_init_struct;

    crm_periph_clock_enable(gpio_clock(RGB_SDB_PORT), TRUE);
    gpio_default_para_init(&gpio_init_struct);
    gpio_init_struct.gpio_pins = (uint16_t)(1U << RGB_SDB_PIN);
    gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
    gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
    gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
    gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
    gpio_init(RGB_SDB_PORT, &gpio_init_struct);
    initialized = true;
  }
  gpio_bits_write(RGB_SDB_PORT, (uint16_t)(1U << RGB_SDB_PIN),
                  enable ? TRUE : FALSE);
#else
  (void)enable;
#endif
}

#endif
