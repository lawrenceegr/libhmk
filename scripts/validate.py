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

import json
import os
import utils

Import("env")

keyboard = env["PIOENV"]

# Load driver (only for validation)
utils.get_driver(keyboard)

# Load JSON files and validate
kb_json = utils.get_kb_json(keyboard)

# Validate default keymaps
default_keymaps = utils.resolve_default_keymaps(kb_json)
if len(default_keymaps) != kb_json.keyboard.num_profiles:
    raise ValueError(
        f"Expected default keymaps to have {kb_json.keyboard.num_profiles} profiles"
    )
for keymap in default_keymaps:
    if len(keymap) != kb_json.keyboard.num_layers:
        raise ValueError(
            f"Expected default keymaps to have {kb_json.keyboard.num_layers} layers"
        )
    for layer in keymap:
        if len(layer) != kb_json.keyboard.num_keys:
            raise ValueError(
                f"Expected default keymaps to have {kb_json.keyboard.num_keys} keys"
            )

# Validate RGB configuration
if kb_json.rgb is not None:
    rgb = kb_json.rgb
    if kb_json.hardware.driver != "at32f405xx":
        raise ValueError(
            f"RGB is not supported on driver {kb_json.hardware.driver} (only at32f405xx)"
        )
    if len(rgb.leds) != kb_json.keyboard.num_keys:
        raise ValueError(f"Expected rgb.leds to have {kb_json.keyboard.num_keys} entries")
    if len({d.i2c for d in rgb.drivers}) != len(rgb.drivers):
        raise ValueError("Each RGB driver must be on its own I2C peripheral")
    if any(d.i2c > 2 for d in rgb.drivers):
        raise ValueError("RGB drivers must be on I2C1 or I2C2 (at32f405xx)")
    for i, led in enumerate(rgb.leds):
        if led is not None and led.driver >= len(rgb.drivers):
            raise ValueError(f"rgb.leds[{i}].driver is out of range")
