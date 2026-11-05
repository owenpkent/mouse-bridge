// mouse_report.h
//
// Finds where a mouse keeps its buttons, movement and wheel by reading its HID
// report map, then decodes input reports with that layout. No ESP-IDF
// dependencies, so test/test_mouse_report.c can run it on a PC.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MOUSE_MAX_BUTTONS 5  // the USB mouse report carries five

typedef struct {
    uint32_t bit_offset;
    uint8_t bit_size;  // 0 = the report has no such field
    bool is_signed;
} mouse_field_t;

typedef struct {
    uint8_t map_index;
    uint8_t report_id;  // 0 = the map doesn't use report IDs
    mouse_field_t x, y, wheel;
    int32_t button_bit[MOUSE_MAX_BUTTONS];  // -1 = no such button
} mouse_layout_t;

typedef struct {
    uint8_t buttons;  // bit0 = left, bit1 = right, bit2 = middle, ...
    int32_t x, y, wheel;
} mouse_report_t;

// Adds a layout for each input report in the map with relative X and Y. Returns
// how many it wrote, at most max_layouts. Malformed maps yield what parsed cleanly.
size_t mouse_parse_report_map(const uint8_t *map, size_t map_len, uint8_t map_index,
                              mouse_layout_t *layouts, size_t max_layouts);

const mouse_layout_t *mouse_layout_find(const mouse_layout_t *layouts, size_t count,
                                        uint8_t map_index, uint8_t report_id);

// data excludes the report ID byte. Fields beyond len read as zero.
void mouse_decode(const mouse_layout_t *layout, const uint8_t *data, size_t len,
                  mouse_report_t *out);

// Boot protocol: buttons, x, y. Returns false if the report is too short.
bool mouse_decode_boot(const uint8_t *data, size_t len, mouse_report_t *out);

// Sends one USB mouse report. Returns false if it couldn't be sent.
typedef bool (*mouse_send_fn)(void *ctx, uint8_t buttons, int8_t x, int8_t y, int8_t wheel);

typedef struct {
    uint8_t want;  // what the mouse reports
    uint8_t sent;  // what the computer currently sees
} mouse_forward_t;

// Forwards one report, or just retries a pending button change when report is NULL.
// Buttons go first, so a click lands where the cursor was when it happened. A
// failed button change stays pending, so a release can't get lost. While unmounted
// nothing is sent, and a held button is replayed to the next computer.
void mouse_forward(mouse_forward_t *state, bool mounted, const mouse_report_t *report,
                   mouse_send_fn send, void *ctx);
