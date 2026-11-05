// bridge_logic.h
//
// Pure bridge logic shared by the Pico and ESP32-S31 firmwares. It has no hardware
// dependencies, so tests/host can run it on a PC. It lives in this folder because
// an Arduino sketch can only include files from its own folder.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BRIDGE_MAX_BUTTONS 5   // a USB boot mouse has five
#define BRIDGE_MAX_MOVE 32767  // far beyond any real report; bounds the work per report

// Class of Device: major class Peripheral (0x05), minor bit 0x80 = pointing device.
// Combo keyboard+pointer devices match too.
static inline bool bridge_is_pointing_device(uint32_t cod) {
  return ((cod >> 8) & 0x1F) == 0x05 && (cod & 0x80) != 0;
}

static inline int32_t bridge_clamp(int32_t v, int32_t limit) {
  return v > limit ? limit : (v < -limit ? -limit : v);
}

// Returns the USB button bit for a Bluetooth library button number, or 0 if the
// button doesn't fit in a USB mouse report.
static inline uint8_t bridge_button_mask(int button_no, int index_base) {
  if (button_no < index_base || button_no - index_base >= BRIDGE_MAX_BUTTONS) {
    return 0;
  }
  return (uint8_t)(1u << (button_no - index_base));
}

typedef struct {
  int32_t x, y, wheel;
} bridge_motion_t;

static inline bridge_motion_t bridge_motion_make(int32_t x, int32_t y, int32_t wheel) {
  bridge_motion_t m;
  m.x = bridge_clamp(x, BRIDGE_MAX_MOVE);
  m.y = bridge_clamp(y, BRIDGE_MAX_MOVE);
  m.wheel = bridge_clamp(wheel, BRIDGE_MAX_MOVE);
  return m;
}

// USB mouse reports carry 8-bit deltas, so large moves are split. Writes the next
// piece and returns true, or returns false when nothing is left.
static inline bool bridge_motion_next(bridge_motion_t *m, int8_t *x, int8_t *y, int8_t *wheel) {
  if (m->x == 0 && m->y == 0 && m->wheel == 0) {
    return false;
  }
  *x = (int8_t)bridge_clamp(m->x, 127);
  *y = (int8_t)bridge_clamp(m->y, 127);
  *wheel = (int8_t)bridge_clamp(m->wheel, 127);
  m->x -= *x;
  m->y -= *y;
  m->wheel -= *wheel;
  return true;
}
