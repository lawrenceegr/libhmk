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

#include "rgb.h"

#if defined(RGB_ENABLED)

#include "eeconfig.h"
#include "hardware/hardware.h"
#include "hardware/i2c_api.h"
#include "keycodes.h"
#include "matrix.h"
#include "wear_leveling.h"

#if defined(RGB_DRIVER_IS31FL3733)
#include "is31fl3733.h"
#define DRIVER_INIT is31fl3733_init
#define DRIVER_SET is31fl3733_set_pwm
#define DRIVER_FLUSH is31fl3733_flush
#define DRIVER_BUSY is31fl3733_busy
#else
#error "Unsupported RGB driver"
#endif

//--------------------------------------------------------------------+
// Saved Settings
//--------------------------------------------------------------------+

// The settings live in their own block at the end of the wear-leveling virtual
// storage, outside `eeconfig_t`, so the main configuration (and its version and
// migrations) is untouched. Factory reset keeps them.
#define RGB_STORE_MAGIC 0x52474231 // "RGB1"
#define RGB_STORE_OFFSET (WL_VIRTUAL_SIZE - 16)

typedef struct __attribute__((packed)) {
  uint32_t magic;
  rgb_settings_t settings;
} rgb_store_t;

_Static_assert(sizeof(eeconfig_t) <= RGB_STORE_OFFSET,
               "eeconfig_t overlaps the RGB settings block");
_Static_assert(RGB_STORE_OFFSET + sizeof(rgb_store_t) <= WL_VIRTUAL_SIZE,
               "RGB settings block does not fit");

//--------------------------------------------------------------------+
// LED Map
//--------------------------------------------------------------------+

#define NO_LED 255

typedef struct {
  // Driver index, NO_LED if the key has no LED
  uint8_t driver;
  // Driver channels of the red, green and blue anodes
  uint8_t r, g, b;
} rgb_led_t;

static const rgb_led_t led_map[NUM_KEYS] = RGB_LED_MAP;
// Key centre on the board: x = 0..255 across the widest row, y same scale
static const uint8_t led_pos[NUM_KEYS][2] = RGB_LED_POS;
static const rgb_settings_t default_settings = RGB_DEFAULT_SETTINGS;

//--------------------------------------------------------------------+
// State
//--------------------------------------------------------------------+

static rgb_settings_t settings;
static bool settings_dirty;
static uint32_t settings_changed_time;

static bool driver_ok[RGB_NUM_DRIVERS];
static uint32_t driver_retry_time;
// Retry a driver that did not respond this often (e.g. not populated)
#define DRIVER_RETRY_INTERVAL 2000

static uint32_t last_frame_time;
// Last time each key was pressed, for the reactive effect
static uint32_t key_press_time[NUM_KEYS];
static bool key_was_pressed[NUM_KEYS];
// Frame of all-off already sent while disabled
static bool off_frame_sent;

//--------------------------------------------------------------------+
// Colour Helpers
//--------------------------------------------------------------------+

typedef struct {
  uint8_t r, g, b;
} rgb_t;

static uint8_t scale8(uint8_t value, uint8_t scale) {
  return (uint8_t)(((uint16_t)value * (uint16_t)(scale + 1)) >> 8);
}

// HSV to RGB with 8-bit hue (0-255 for a full turn)
static rgb_t hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v) {
  if (s == 0)
    return (rgb_t){v, v, v};

  const uint8_t region = h / 43;
  const uint8_t remainder = (uint8_t)((h - region * 43) * 6);
  const uint8_t p = scale8(v, (uint8_t)(255 - s));
  const uint8_t q = scale8(v, (uint8_t)(255 - scale8(s, remainder)));
  const uint8_t t = scale8(v, (uint8_t)(255 - scale8(s, (uint8_t)(255 - remainder))));

  switch (region) {
  case 0:
    return (rgb_t){v, t, p};
  case 1:
    return (rgb_t){q, v, p};
  case 2:
    return (rgb_t){p, v, t};
  case 3:
    return (rgb_t){p, q, v};
  case 4:
    return (rgb_t){t, p, v};
  default:
    return (rgb_t){v, p, q};
  }
}

// Perceptual correction: LED output is roughly linear in PWM, our eyes are not
static uint8_t gamma8(uint8_t x) {
  return (uint8_t)(((uint16_t)x * (uint16_t)x + 255) >> 8);
}

// Animation position that advances faster with higher speed. At speed 128 it
// moves about 50 steps (of 256) per second.
static uint8_t animation_phase(uint32_t now) {
  const uint64_t rate = (uint64_t)settings.speed + 32;
  return (uint8_t)(((uint64_t)now * rate) / 3200);
}

// 0 -> 255 -> 0 triangle, eased so the dark end lingers like breathing
static uint8_t breathe(uint8_t phase) {
  const uint8_t tri =
      phase < 128 ? (uint8_t)(phase * 2) : (uint8_t)((255 - phase) * 2);
  return gamma8(tri);
}

//--------------------------------------------------------------------+
// Effects
//--------------------------------------------------------------------+

static rgb_t render_key(uint8_t key, uint32_t now, uint8_t phase) {
  const uint8_t hue = settings.hue;
  const uint8_t sat = settings.saturation;
  const uint8_t val = settings.brightness;

  switch (settings.effect) {
  case RGB_EFFECT_SOLID:
    return hsv_to_rgb(hue, sat, val);

  case RGB_EFFECT_BREATHING:
    return hsv_to_rgb(hue, sat, scale8(val, breathe(phase)));

  case RGB_EFFECT_RAINBOW_CYCLE:
    return hsv_to_rgb((uint8_t)(hue + phase), sat, val);

  case RGB_EFFECT_RAINBOW_WAVE:
    return hsv_to_rgb((uint8_t)(hue + led_pos[key][0] - phase), sat, val);

  case RGB_EFFECT_REACTIVE: {
    if (key_matrix[key].is_pressed)
      return hsv_to_rgb(hue, sat, val);
    const uint32_t elapsed = now - key_press_time[key];
    if (key_press_time[key] == 0 || elapsed >= RGB_REACTIVE_FADE)
      return (rgb_t){0, 0, 0};
    const uint8_t fade =
        (uint8_t)(255 - (elapsed * 255) / RGB_REACTIVE_FADE);
    return hsv_to_rgb(hue, sat, scale8(val, gamma8(fade)));
  }

  case RGB_EFFECT_TRAVEL: {
    // Hall effect: the deeper the key, the brighter, shifting the hue
    const uint8_t d = key_matrix[key].distance;
    return hsv_to_rgb((uint8_t)(hue + d / 3), sat, scale8(val, d));
  }

  default:
    return (rgb_t){0, 0, 0};
  }
}

static void track_presses(uint32_t now) {
  for (uint8_t key = 0; key < NUM_KEYS; key++) {
    const bool pressed = key_matrix[key].is_pressed;
    if (pressed && !key_was_pressed[key])
      // `| 1` so a press at time 0 is not mistaken for "never pressed"
      key_press_time[key] = now | 1;
    key_was_pressed[key] = pressed;
  }
}

static void render_frame(uint32_t now) {
  const uint8_t phase = animation_phase(now);

  for (uint8_t key = 0; key < NUM_KEYS; key++) {
    const rgb_led_t *led = &led_map[key];
    if (led->driver == NO_LED || !driver_ok[led->driver])
      continue;

    rgb_t c = settings.enabled ? render_key(key, now, phase) : (rgb_t){0, 0, 0};
    DRIVER_SET(led->driver, led->r, gamma8(c.r));
    DRIVER_SET(led->driver, led->g, gamma8(c.g));
    DRIVER_SET(led->driver, led->b, gamma8(c.b));
  }
}

//--------------------------------------------------------------------+
// Settings
//--------------------------------------------------------------------+

static void load_settings(void) {
  rgb_store_t store;
  memcpy(&store, &wl_cache[RGB_STORE_OFFSET], sizeof(store));

  if (store.magic == RGB_STORE_MAGIC &&
      store.settings.effect < RGB_EFFECT_COUNT)
    settings = store.settings;
  else
    settings = default_settings;

  if (settings.effect >= RGB_EFFECT_COUNT)
    settings.effect = RGB_EFFECT_SOLID;
}

static void save_settings(void) {
  const rgb_store_t store = {.magic = RGB_STORE_MAGIC, .settings = settings};
  wear_leveling_write(RGB_STORE_OFFSET, &store, sizeof(store));
}

static void settings_changed(void) {
  settings_dirty = true;
  settings_changed_time = timer_read();
  off_frame_sent = false;
}

static uint8_t step_up(uint8_t value) {
  return value > 255 - RGB_STEP ? 255 : (uint8_t)(value + RGB_STEP);
}

static uint8_t step_down(uint8_t value) {
  return value < RGB_STEP ? 0 : (uint8_t)(value - RGB_STEP);
}

void rgb_process_keycode(uint8_t keycode) {
  switch (keycode) {
  case SP_RGB_TOGGLE:
    settings.enabled = !settings.enabled;
    break;
  case SP_RGB_EFFECT_NEXT:
    settings.effect = (uint8_t)((settings.effect + 1) % RGB_EFFECT_COUNT);
    break;
  case SP_RGB_EFFECT_PREV:
    settings.effect = (uint8_t)((settings.effect + RGB_EFFECT_COUNT - 1) %
                                RGB_EFFECT_COUNT);
    break;
  case SP_RGB_HUE_UP:
    // Hue wraps around
    settings.hue = (uint8_t)(settings.hue + RGB_STEP);
    break;
  case SP_RGB_HUE_DOWN:
    settings.hue = (uint8_t)(settings.hue - RGB_STEP);
    break;
  case SP_RGB_SAT_UP:
    settings.saturation = step_up(settings.saturation);
    break;
  case SP_RGB_SAT_DOWN:
    settings.saturation = step_down(settings.saturation);
    break;
  case SP_RGB_BRIGHTNESS_UP:
    settings.brightness = step_up(settings.brightness);
    break;
  case SP_RGB_BRIGHTNESS_DOWN:
    settings.brightness = step_down(settings.brightness);
    break;
  case SP_RGB_SPEED_UP:
    settings.speed = step_up(settings.speed);
    break;
  case SP_RGB_SPEED_DOWN:
    settings.speed = step_down(settings.speed);
    break;
  default:
    return;
  }
  // Turning any knob also turns the lights on
  if (keycode != SP_RGB_TOGGLE)
    settings.enabled = true;
  settings_changed();
}

//--------------------------------------------------------------------+
// RGB API
//--------------------------------------------------------------------+

static void init_drivers(void) {
  for (uint8_t i = 0; i < RGB_NUM_DRIVERS; i++)
    if (!driver_ok[i])
      driver_ok[i] = DRIVER_INIT(i);
  driver_retry_time = timer_read();
}

void rgb_init(void) {
  load_settings();
  led_driver_enable(true);
  // The drivers need about 1 ms after SDB goes high
  timer_delay(2);
  init_drivers();
  last_frame_time = timer_read();
}

void rgb_task(void) {
  const uint32_t now = timer_read();

  track_presses(now);

  if (settings_dirty && timer_elapsed(settings_changed_time) >= RGB_SAVE_DELAY) {
    save_settings();
    settings_dirty = false;
  }

  bool all_ok = true;
  for (uint8_t i = 0; i < RGB_NUM_DRIVERS; i++)
    all_ok = all_ok && driver_ok[i];
  if (!all_ok && timer_elapsed(driver_retry_time) >= DRIVER_RETRY_INTERVAL)
    init_drivers();

  if (timer_elapsed(last_frame_time) < RGB_FRAME_INTERVAL)
    return;

  // Wait for every driver to finish the previous frame before drawing the next,
  // so a frame buffer is never changed while it is being sent.
  for (uint8_t i = 0; i < RGB_NUM_DRIVERS; i++)
    if (driver_ok[i] && DRIVER_BUSY(i))
      return;

  last_frame_time = now;
  if (!settings.enabled && off_frame_sent)
    return;

  render_frame(now);
  for (uint8_t i = 0; i < RGB_NUM_DRIVERS; i++)
    if (driver_ok[i])
      DRIVER_FLUSH(i);
  off_frame_sent = !settings.enabled;
}

#endif
