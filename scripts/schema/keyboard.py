# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program. If not, see <https://www.gnu.org/licenses/>.

from enum import Enum
from pydantic import BaseModel, Field, NonNegativeInt, PositiveFloat, PositiveInt


class KeyboardUSBPort(str, Enum):
    FULL_SPEED = "fs"
    HIGH_SPEED = "hs"


# USB Configuration
class KeyboardUSB(BaseModel):
    # USB Vendor ID
    vid: str = Field(pattern=r"^0x[0-9A-Fa-f]{4}$")
    # USB Product ID
    pid: str = Field(pattern=r"^0x[0-9A-Fa-f]{4}$")
    port: KeyboardUSBPort


# Keyboard Configuration
class KeyboardKeyboard(BaseModel):
    num_profiles: int = Field(ge=1, le=8)
    num_layers: int = Field(ge=1, le=8)
    num_keys: int = Field(ge=1, le=256)
    num_advanced_keys: int = Field(ge=1, le=64)
    # Maximum number of Dynamic Keystroke bindings per key. Higher values may require higher storage sizes.
    num_dynamic_keystroke_max_bindings: int = Field(ge=4, le=64, default=4)
    num_macro_nodes: int = Field(ge=1, le=255, default=128)


# Hardware Configuration
class KeyboardHardware(BaseModel):
    # High-speed external oscillator value in Hz
    hse_value: PositiveInt
    # Keyboard driver name
    driver: str


# Raw ADC input configuration
class KeyboardAnalogRaw(BaseModel):
    # Array of raw ADC input channels. If a string is provided, it is used as the GPIO pin name
    input: list[NonNegativeInt | str]
    # Array of key index mappings for the raw ADC inputs
    vector: list[NonNegativeInt]


# Analog Multiplexer ADC input configuration
class KeyboardAnalogMux(BaseModel):
    # Array of GPIO pin names for the multiplexer select lines
    select: list[str]
    # Array of ADC input channels for the multiplexers. If a string is provided, it is used as the GPIO pin name
    input: list[NonNegativeInt | str]
    # Mapping from multiplexers to ADC input channels
    matrix: list[list[NonNegativeInt]]


# Analog Configuration
class KeyboardAnalog(BaseModel):
    # ADC resolution for this keyboard. A higher value means higher accuracy but slower matrix scans. Default to the maximum resolution supported by the MCU if not provided.
    adc_resolution: PositiveInt | None = None
    # Set to true if the ADC value is inversely proportional to the travel distance of the keys
    invert_adc: bool = False
    # Delay in microseconds between ADC scans
    delay: int | None = None
    raw: KeyboardAnalogRaw | None = None
    mux: KeyboardAnalogMux | None = None


# Calibration Configuration
class KeyboardCalibration(BaseModel):
    # See `include/lib/eeconfig.h`
    initial_rest_value: NonNegativeInt
    # See `include/lib/eeconfig.h`
    initial_bottom_out_threshold: NonNegativeInt


# Wear leveling Configuration
class KeyboardWearLeveling(BaseModel):
    # Size of the virtual persistent storage in bytes. There must be enough RAM of this size to hold the entire virtual storage.
    virtual_size: int = Field(ge=1, le=16384)
    # Size of the write log in bytes
    write_log_size: int = Field(ge=1, le=65536)


class KeyboardLayoutKey(BaseModel):
    key: NonNegativeInt
    w: PositiveFloat | None = None
    h: PositiveFloat | None = None
    x: float | None = None
    y: float | None = None
    # Option key and value pairs for the corresponding labels
    option: tuple[int, int] | None = None


# Keyboard Layout
class KeyboardLayout(BaseModel):
    # Labels for each layout option. Use a string for a toggle option, or an array for a select option
    labels: list[str | list[str]] | None = None
    # Metadata for how the keyboard should be rendered in the web configurator
    keymap: list[list[KeyboardLayoutKey]]


# Actuation Configuration
class KeyboardActuation(BaseModel):
    # Default actuation point
    actuation_point: int = Field(ge=0, le=255)


class KeyboardRGBDriverType(str, Enum):
    IS31FL3733 = "is31fl3733"


# One LED driver chip on its own I2C bus
class KeyboardRGBDriver(BaseModel):
    # I2C peripheral number (e.g. 1 for I2C1)
    i2c: int = Field(ge=1, le=3)
    # I2C pins, as GPIO pin names (e.g. "B7")
    sda: str = Field(pattern=r"^[A-K][0-9]{1,2}$")
    scl: str = Field(pattern=r"^[A-K][0-9]{1,2}$")
    # Alternate function (IOMUX) number of the SDA/SCL pins for this I2C peripheral
    mux: int = Field(ge=0, le=15, default=4)
    # 7-bit I2C address (IS31FL3733 with ADDR1 = ADDR2 = GND: 0x50 = 80)
    address: int = Field(ge=0, le=127, default=0x50)


# One RGB LED: driver index, the CS pin of its common cathode and the SW pins of
# its red/green/blue anodes, numbered as on the IS31FL3733 (CS1..CS16, SW1..SW12).
class KeyboardRGBLed(BaseModel):
    driver: NonNegativeInt
    cs: int = Field(ge=1, le=16)
    r: int = Field(ge=1, le=12)
    g: int = Field(ge=1, le=12)
    b: int = Field(ge=1, le=12)


# Default RGB settings (until changed with keycodes)
class KeyboardRGBDefaults(BaseModel):
    enabled: bool = True
    # Effect index, see `rgb_effect_t` in include/rgb.h
    effect: int = Field(ge=0, le=255, default=0)
    hue: int = Field(ge=0, le=255, default=0)
    saturation: int = Field(ge=0, le=255, default=255)
    brightness: int = Field(ge=0, le=255, default=128)
    speed: int = Field(ge=0, le=255, default=128)


# Per-key RGB lighting configuration
class KeyboardRGB(BaseModel):
    driver: KeyboardRGBDriverType
    drivers: list[KeyboardRGBDriver] = Field(min_length=1, max_length=4)
    # Shutdown pin shared by all drivers (driven high to enable them)
    sdb: str | None = Field(default=None, pattern=r"^[A-K][0-9]{1,2}$")
    # Global current control (GCC, 0-255). Peak LED current = 840 / R_ISET * GCC / 256 mA.
    global_current: int = Field(ge=0, le=255, default=64)
    # One entry per key index (length must equal num_keys); null for a key without an LED.
    leds: list[KeyboardRGBLed | None]
    defaults: KeyboardRGBDefaults = KeyboardRGBDefaults()


# keyboard.json Schema
class Keyboard(BaseModel):
    name: str
    manufacturer: str
    maintainer: str
    usb: KeyboardUSB
    keyboard: KeyboardKeyboard
    hardware: KeyboardHardware
    analog: KeyboardAnalog
    calibration: KeyboardCalibration
    wear_leveling: KeyboardWearLeveling | None = None
    layout: KeyboardLayout
    # Default keymap
    keymap: list[list[str]] | None = None
    # Default keymaps for each profile. If not specified, the default keymap will be used for all profiles.
    keymaps: list[list[list[str]]] | None = None
    actuation: KeyboardActuation | None = None
    rgb: KeyboardRGB | None = None
