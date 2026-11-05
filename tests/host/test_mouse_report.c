// Tests the ESP32-S31 report parser, decoder and USB forwarding.

#include <string.h>

#include "check.h"
#include "mouse_report.h"

#define PARSE(map, out) mouse_parse_report_map((map), sizeof(map), 0, (out), 4)

// Common 3-button mouse with a report ID: 8-bit X, Y and wheel.
static const uint8_t MOUSE_WITH_ID[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x02,        //   Report ID (2)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (1)
    0x29, 0x03,        //     Usage Maximum (3)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x03,        //     Report Count (3)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data, Var, Abs)
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x05,        //     Report Size (5)
    0x81, 0x03,        //     Input (Const) padding
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x03,        //     Report Count (3)
    0x81, 0x06,        //     Input (Data, Var, Rel)
    0xC0,              //   End Collection
    0xC0,              // End Collection
};

// Higher-end shape: 5 buttons in 16 bits, 12-bit X and Y, no report ID.
static const uint8_t MOUSE_12BIT[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x10, 0x75, 0x01, 0x81, 0x02,  // 16 buttons
    0x05, 0x01, 0x16, 0x01, 0xF8, 0x26, 0xFF, 0x07,  // logical -2047..2047
    0x75, 0x0C, 0x95, 0x02, 0x09, 0x30, 0x09, 0x31, 0x81, 0x06,  // 12-bit X, Y
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x09, 0x38, 0x81, 0x06,  // wheel
    0xC0, 0xC0,
};

// Keyboard (ID 1) and mouse (ID 2) in one map; the mouse has no wheel.
static const uint8_t COMBO[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0xC0,
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x02, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x02, 0x81, 0x02,
    0x75, 0x06, 0x95, 0x01, 0x81, 0x03,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F,
    0x75, 0x08, 0x95, 0x02, 0x81, 0x06, 0xC0,
};

// Absolute X and Y, like a touch panel: not something this bridge can forward.
static const uint8_t ABSOLUTE[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x15, 0x00, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x02, 0x81, 0x02, 0xC0,
};

static void test_mouse_with_id(void) {
    mouse_layout_t out[4];
    CHECK(PARSE(MOUSE_WITH_ID, out) == 1);
    CHECK(out[0].report_id == 2);
    const uint8_t report[] = {0x05, 0xFB, 0x02, 0xFF};  // left + middle, x=-5, y=2, wheel=-1
    mouse_report_t r;
    mouse_decode(&out[0], report, sizeof(report), &r);
    CHECK(r.buttons == 0x05);
    CHECK(r.x == -5);
    CHECK(r.y == 2);
    CHECK(r.wheel == -1);
}

static void test_12bit_fields(void) {
    mouse_layout_t out[4];
    CHECK(PARSE(MOUSE_12BIT, out) == 1);
    CHECK(out[0].report_id == 0);
    CHECK(out[0].x.bit_offset == 16 && out[0].x.bit_size == 12);
    CHECK(out[0].y.bit_offset == 28 && out[0].y.bit_size == 12);
    CHECK(out[0].wheel.bit_offset == 40);
    // Button 5 pressed, x = -2 (0xFFE), y = 3 (0x003), wheel = 1. Buttons 6+ are not forwarded.
    const uint8_t report[] = {0x10, 0xFF, 0xFE, 0x3F, 0x00, 0x01};
    mouse_report_t r;
    mouse_decode(&out[0], report, sizeof(report), &r);
    CHECK(r.buttons == 0x10);
    CHECK(r.x == -2);
    CHECK(r.y == 3);
    CHECK(r.wheel == 1);
}

static void test_combo_skips_keyboard(void) {
    mouse_layout_t out[4];
    CHECK(PARSE(COMBO, out) == 1);
    CHECK(out[0].report_id == 2);
    CHECK(out[0].wheel.bit_size == 0);
    CHECK(mouse_layout_find(out, 1, 0, 1) == NULL);
    CHECK(mouse_layout_find(out, 1, 1, 2) == NULL);  // wrong map
    const mouse_layout_t *m = mouse_layout_find(out, 1, 0, 2);
    CHECK(m != NULL);
    const uint8_t report[] = {0x02, 0x01, 0xFF};
    mouse_report_t r;
    mouse_decode(m, report, sizeof(report), &r);
    CHECK(r.buttons == 0x02 && r.x == 1 && r.y == -1 && r.wheel == 0);
}

static void test_absolute_rejected(void) {
    mouse_layout_t out[4];
    CHECK(PARSE(ABSOLUTE, out) == 0);
}

static void test_global_push_pop(void) {
    const uint8_t map[] = {
        0x05, 0x01,              // Usage Page (Generic Desktop)
        0x15, 0x81, 0x75, 0x08, 0x95, 0x02,  // signed, 8 bits, count 2
        0xA4,                    // Push
        0x05, 0x09, 0x75, 0x01, 0x95, 0x08,  // Button page, 1 bit, count 8
        0x19, 0x01, 0x29, 0x08, 0x81, 0x02,
        0xB4,                    // Pop back to Generic Desktop, 8 bits, count 2
        0x09, 0x30, 0x09, 0x31, 0x81, 0x06,
    };
    mouse_layout_t out[4];
    CHECK(PARSE(map, out) == 1);
    CHECK(out[0].button_bit[0] == 0);
    CHECK(out[0].x.bit_offset == 8 && out[0].x.bit_size == 8);
    CHECK(out[0].y.bit_offset == 16);
}

static void test_four_byte_usage(void) {
    const uint8_t map[] = {
        0x15, 0x81, 0x75, 0x08, 0x95, 0x02,
        0x0B, 0x30, 0x00, 0x01, 0x00,  // Usage (Generic Desktop: X)
        0x0B, 0x31, 0x00, 0x01, 0x00,  // Usage (Generic Desktop: Y)
        0x81, 0x06,
    };
    mouse_layout_t out[4];
    CHECK(PARSE(map, out) == 1);
    CHECK(out[0].y.bit_offset == 8);
}

static void test_long_item_skipped(void) {
    const uint8_t map[] = {
        0xFE, 0x02, 0x10, 0xAA, 0xBB,  // long item with 2 data bytes
        0x05, 0x01, 0x15, 0x81, 0x75, 0x08, 0x95, 0x02, 0x09, 0x30, 0x09, 0x31, 0x81, 0x06,
    };
    mouse_layout_t out[4];
    CHECK(PARSE(map, out) == 1);
}

static void test_every_truncation_is_safe(void) {
    mouse_layout_t out[4];
    for (size_t len = 0; len < sizeof(MOUSE_WITH_ID); len++) {
        size_t n = mouse_parse_report_map(MOUSE_WITH_ID, len, 0, out, 4);
        CHECK(n <= 1);
    }
    const uint8_t long_past_end[] = {0xFE, 0xFF};
    CHECK(PARSE(long_past_end, out) == 0);
}

static void test_absurd_sizes_stop_parsing(void) {
    const uint8_t map[] = {
        0x05, 0x01, 0x77, 0xFF, 0xFF, 0xFF, 0x7F,  // Report Size 0x7FFFFFFF
        0x97, 0xFF, 0xFF, 0xFF, 0x7F,              // Report Count 0x7FFFFFFF
        0x09, 0x30, 0x09, 0x31, 0x81, 0x06,
    };
    mouse_layout_t out[4];
    CHECK(PARSE(map, out) == 0);
}

static void test_short_report_reads_zero(void) {
    mouse_layout_t out[4];
    CHECK(PARSE(MOUSE_WITH_ID, out) == 1);
    const uint8_t report[] = {0x01, 0x10};
    mouse_report_t r;
    mouse_decode(&out[0], report, sizeof(report), &r);
    CHECK(r.buttons == 0x01 && r.x == 0x10 && r.y == 0 && r.wheel == 0);
    mouse_decode(&out[0], report, 0, &r);
    CHECK(r.buttons == 0 && r.x == 0);
}

static void test_layout_limit_respected(void) {
    mouse_layout_t one;
    CHECK(mouse_parse_report_map(MOUSE_WITH_ID, sizeof(MOUSE_WITH_ID), 0, &one, 0) == 0);
}

static void test_boot_report(void) {
    const uint8_t report[] = {0xE3, 0xFE, 0x04};
    mouse_report_t r;
    CHECK(mouse_decode_boot(report, sizeof(report), &r));
    CHECK(r.buttons == 0x03 && r.x == -2 && r.y == 4 && r.wheel == 0);
    CHECK(!mouse_decode_boot(report, 2, &r));
}

// A fake USB port that records what it sends and can be told to fail.
typedef struct {
    int calls;
    int fail_calls;  // fail this many sends, then work
    uint8_t buttons[8];
    int x, y, wheel;
} fake_usb_t;

static bool fake_send(void *ctx, uint8_t buttons, int8_t x, int8_t y, int8_t wheel) {
    fake_usb_t *usb = ctx;
    if (usb->fail_calls > 0) {
        usb->fail_calls--;
        return false;
    }
    if (usb->calls < 8) {
        usb->buttons[usb->calls] = buttons;
    }
    usb->calls++;
    usb->x += x;
    usb->y += y;
    usb->wheel += wheel;
    return true;
}

static void test_forward_click_goes_before_motion(void) {
    mouse_forward_t state = {0};
    fake_usb_t usb = {0};
    const mouse_report_t r = {.buttons = 0x01, .x = 10, .y = -4, .wheel = 1};
    mouse_forward(&state, true, &r, fake_send, &usb);
    CHECK(usb.calls == 2);
    CHECK(usb.buttons[0] == 0x01);  // the press, with no movement
    CHECK(usb.buttons[1] == 0x01);  // then the move, still pressed
    CHECK(usb.x == 10 && usb.y == -4 && usb.wheel == 1);
    CHECK(state.sent == 0x01);
}

static void test_forward_no_change_sends_nothing(void) {
    mouse_forward_t state = {0};
    fake_usb_t usb = {0};
    const mouse_report_t r = {0};
    mouse_forward(&state, true, &r, fake_send, &usb);
    mouse_forward(&state, true, NULL, fake_send, &usb);
    CHECK(usb.calls == 0);
}

static void test_forward_large_move_is_split(void) {
    mouse_forward_t state = {0};
    fake_usb_t usb = {0};
    const mouse_report_t r = {.x = 300};
    mouse_forward(&state, true, &r, fake_send, &usb);
    CHECK(usb.calls == 3);
    CHECK(usb.x == 300);
}

// The failure that matters most: a lost release leaves a click stuck on the computer.
static void test_forward_failed_release_is_retried(void) {
    mouse_forward_t state = {0};
    fake_usb_t usb = {0};
    const mouse_report_t press = {.buttons = 0x01};
    const mouse_report_t release = {.buttons = 0x00};
    mouse_forward(&state, true, &press, fake_send, &usb);
    CHECK(state.sent == 0x01);

    usb.fail_calls = 2;
    mouse_forward(&state, true, &release, fake_send, &usb);
    CHECK(state.sent == 0x01);
    mouse_forward(&state, true, NULL, fake_send, &usb);
    CHECK(state.sent == 0x01);
    mouse_forward(&state, true, NULL, fake_send, &usb);
    CHECK(state.sent == 0x00);
    CHECK(usb.calls == 2);
    CHECK(usb.buttons[1] == 0x00);
}

static void test_forward_failed_press_drops_motion(void) {
    mouse_forward_t state = {0};
    fake_usb_t usb = {.fail_calls = 1};
    const mouse_report_t r = {.buttons = 0x01, .x = 10};
    mouse_forward(&state, true, &r, fake_send, &usb);
    CHECK(usb.calls == 0 && usb.x == 0);
    CHECK(state.want == 0x01 && state.sent == 0);
}

static void test_forward_failed_motion_stops_early(void) {
    mouse_forward_t state = {0};
    fake_usb_t usb = {.fail_calls = 1};
    const mouse_report_t r = {.x = 300};
    mouse_forward(&state, true, &r, fake_send, &usb);
    CHECK(usb.calls == 0 && usb.x == 0);
}

static void test_forward_unplugged_sends_nothing(void) {
    mouse_forward_t state = {0};
    fake_usb_t usb = {0};
    const mouse_report_t r = {.buttons = 0x01, .x = 10};
    mouse_forward(&state, false, &r, fake_send, &usb);
    CHECK(usb.calls == 0);
    CHECK(state.want == 0x01 && state.sent == 0);
}

// Moving the bridge while a button is held: the next computer sees it held.
static void test_forward_held_button_replays_after_hot_swap(void) {
    mouse_forward_t state = {0};
    fake_usb_t usb = {0};
    const mouse_report_t press = {.buttons = 0x01};
    mouse_forward(&state, true, &press, fake_send, &usb);
    CHECK(usb.calls == 1);

    mouse_forward(&state, false, NULL, fake_send, &usb);
    CHECK(state.sent == 0);

    mouse_forward(&state, true, NULL, fake_send, &usb);
    CHECK(usb.calls == 2);
    CHECK(usb.buttons[1] == 0x01);
    CHECK(state.sent == 0x01);
}

// Released while unplugged: the next computer must never see a press.
static void test_forward_release_while_unplugged_stays_released(void) {
    mouse_forward_t state = {0};
    fake_usb_t usb = {0};
    const mouse_report_t press = {.buttons = 0x01};
    const mouse_report_t release = {.buttons = 0x00};
    mouse_forward(&state, true, &press, fake_send, &usb);
    mouse_forward(&state, false, &release, fake_send, &usb);
    mouse_forward(&state, true, NULL, fake_send, &usb);
    CHECK(usb.calls == 1);
    CHECK(state.sent == 0);
}

int main(void) {
    test_mouse_with_id();
    test_12bit_fields();
    test_combo_skips_keyboard();
    test_absolute_rejected();
    test_global_push_pop();
    test_four_byte_usage();
    test_long_item_skipped();
    test_every_truncation_is_safe();
    test_absurd_sizes_stop_parsing();
    test_short_report_reads_zero();
    test_layout_limit_respected();
    test_boot_report();
    test_forward_click_goes_before_motion();
    test_forward_no_change_sends_nothing();
    test_forward_large_move_is_split();
    test_forward_failed_release_is_retried();
    test_forward_failed_press_drops_motion();
    test_forward_failed_motion_stops_early();
    test_forward_unplugged_sends_nothing();
    test_forward_held_button_replays_after_hot_swap();
    test_forward_release_while_unplugged_stays_released();
    return check_result("mouse_report");
}
