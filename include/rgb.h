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
// RGB Configuration
//--------------------------------------------------------------------+

#if !defined(RGB_FRAME_INTERVAL)
// Time between rendered frames in milliseconds (about 60 FPS)
#define RGB_FRAME_INTERVAL 16
#endif

#if !defined(RGB_SAVE_DELAY)
// Settings are written to flash this long after the last change
#define RGB_SAVE_DELAY 3000
#endif

#if !defined(RGB_STEP)
// Step for hue/saturation/brightness/speed keycodes
#define RGB_STEP 16
#endif

#if !defined(RGB_REACTIVE_FADE)
// Fade-out time of the reactive effect in milliseconds
#define RGB_REACTIVE_FADE 600
#endif

//--------------------------------------------------------------------+
// RGB Types
//--------------------------------------------------------------------+

typedef enum {
  // Every key in the selected colour
  RGB_EFFECT_SOLID = 0,
  // Selected colour fading in and out
  RGB_EFFECT_BREATHING,
  // Every key cycling through the hues together
  RGB_EFFECT_RAINBOW_CYCLE,
  // Hues scrolling across the board from left to right
  RGB_EFFECT_RAINBOW_WAVE,
  // Keys light up when pressed and fade out after release
  RGB_EFFECT_REACTIVE,
  // Brightness and hue follow how far each key is pressed (analog)
  RGB_EFFECT_TRAVEL,
  RGB_EFFECT_COUNT,
} rgb_effect_t;

typedef struct __attribute__((packed)) {
  bool enabled;
  uint8_t effect;
  uint8_t hue;
  uint8_t saturation;
  uint8_t brightness;
  uint8_t speed;
} rgb_settings_t;

//--------------------------------------------------------------------+
// RGB API
//--------------------------------------------------------------------+

#if defined(RGB_ENABLED)

/**
 * @brief Initialize the LED drivers and load the saved settings
 *
 * @return None
 */
void rgb_init(void);

/**
 * @brief Render and send frames, save changed settings. Call from the main loop.
 *
 * @return None
 */
void rgb_task(void);

/**
 * @brief Handle an RGB keycode press (`SP_RGB_*`)
 *
 * @param keycode Keycode
 *
 * @return None
 */
void rgb_process_keycode(uint8_t keycode);

#else

static inline void rgb_init(void) {}
static inline void rgb_task(void) {}
static inline void rgb_process_keycode(uint8_t keycode) { (void)keycode; }

#endif
