// Tests firmware/mouse_bridge/bridge_logic.h, which both firmwares use.

#include <limits.h>

#include "bridge_logic.h"
#include "check.h"

// CoD = service class (bits 13+), major class (bits 8-12), minor class (bits 2-7).
static void test_pointing_device(void) {
    CHECK(bridge_is_pointing_device(0x002580));   // mouse
    CHECK(bridge_is_pointing_device(0x0005C0));   // combo keyboard + pointer
    CHECK(!bridge_is_pointing_device(0x002540));  // keyboard only
    CHECK(!bridge_is_pointing_device(0x000508));  // gamepad
    CHECK(!bridge_is_pointing_device(0x240404));  // headset
    CHECK(!bridge_is_pointing_device(0x000480));  // audio major class, same minor bit
    CHECK(!bridge_is_pointing_device(0));
}

static void test_clamp(void) {
    CHECK(bridge_clamp(5, 127) == 5);
    CHECK(bridge_clamp(127, 127) == 127);
    CHECK(bridge_clamp(128, 127) == 127);
    CHECK(bridge_clamp(-127, 127) == -127);
    CHECK(bridge_clamp(-128, 127) == -127);
    CHECK(bridge_clamp(INT32_MIN, 127) == -127);
    CHECK(bridge_clamp(INT32_MAX, 127) == 127);
}

static void test_button_mask(void) {
    CHECK(bridge_button_mask(0, 0) == 0x01);
    CHECK(bridge_button_mask(4, 0) == 0x10);
    CHECK(bridge_button_mask(5, 0) == 0);
    CHECK(bridge_button_mask(-1, 0) == 0);
    CHECK(bridge_button_mask(1, 1) == 0x01);  // libraries that number from 1
    CHECK(bridge_button_mask(0, 1) == 0);
    CHECK(bridge_button_mask(5, 1) == 0x10);
    CHECK(bridge_button_mask(6, 1) == 0);
    CHECK(bridge_button_mask(INT_MIN, 1) == 0);
    CHECK(bridge_button_mask(INT_MAX, 0) == 0);
}

static void test_motion_none(void) {
    bridge_motion_t m = bridge_motion_make(0, 0, 0);
    int8_t x, y, w;
    CHECK(!bridge_motion_next(&m, &x, &y, &w));
}

static void test_motion_small(void) {
    bridge_motion_t m = bridge_motion_make(5, -3, 1);
    int8_t x, y, w;
    CHECK(bridge_motion_next(&m, &x, &y, &w));
    CHECK(x == 5 && y == -3 && w == 1);
    CHECK(!bridge_motion_next(&m, &x, &y, &w));
}

static void test_motion_split_preserves_total(void) {
    bridge_motion_t m = bridge_motion_make(300, -128, 127);
    int8_t x, y, w;
    int sum_x = 0, sum_y = 0, sum_w = 0, pieces = 0;
    while (bridge_motion_next(&m, &x, &y, &w)) {
        CHECK(x != -128 && y != -128 && w != -128);  // the USB mouse range is -127..127
        sum_x += x;
        sum_y += y;
        sum_w += w;
        pieces++;
    }
    CHECK(sum_x == 300 && sum_y == -128 && sum_w == 127);
    CHECK(pieces == 3);  // 127 + 127 + 46
}

static void test_motion_extremes_are_bounded(void) {
    bridge_motion_t m = bridge_motion_make(INT32_MAX, INT32_MIN, 0);
    CHECK(m.x == BRIDGE_MAX_MOVE && m.y == -BRIDGE_MAX_MOVE);
    int8_t x, y, w;
    int pieces = 0;
    while (bridge_motion_next(&m, &x, &y, &w)) {
        pieces++;
    }
    CHECK(pieces == 259);  // 32767 / 127 rounded up
}

int main(void) {
    test_pointing_device();
    test_clamp();
    test_button_mask();
    test_motion_none();
    test_motion_small();
    test_motion_split_preserves_total();
    test_motion_extremes_are_bounded();
    return check_result("bridge_logic");
}
