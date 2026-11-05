// libFuzzer harness for the path Bluetooth data takes through the ESP32-S31 firmware:
// report map parsing, input report decoding, and forwarding to USB. A mouse supplies
// these bytes, so none of them can be trusted. See tests/fuzz/README.md.
//
// Input format: 2-byte little-endian map length (taken modulo what's left), the
// report map, then one input report. The last byte also chooses which USB sends fail.
//
// The map and report are copied into separate exact-size buffers, so AddressSanitizer
// catches a read one byte past either of them.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bridge_logic.h"
#include "mouse_report.h"

#define REQUIRE(cond)                                                   \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "Invariant failed, line %d: %s\n", __LINE__, #cond); \
            abort();                                                    \
        }                                                               \
    } while (0)

#define MAX_LAYOUTS 4

typedef struct {
    bool failing;
    uint32_t fail_bits;
    uint32_t calls;
    uint8_t last_buttons;
    int64_t x, y, wheel;
} sink_t;

static bool sink_send(void *ctx, uint8_t buttons, int8_t x, int8_t y, int8_t wheel) {
    sink_t *s = ctx;
    REQUIRE((buttons & ~0x1Fu) == 0);
    REQUIRE(x != -128 && y != -128 && wheel != -128);
    uint32_t call = s->calls++;
    REQUIRE(s->calls <= 1024);  // a report must never cause unbounded USB traffic
    if (s->failing && ((s->fail_bits >> (call % 8)) & 1u)) {
        return false;
    }
    s->last_buttons = buttons;
    s->x += x;
    s->y += y;
    s->wheel += wheel;
    return true;
}

static void check_field(const mouse_field_t *f, int32_t value) {
    if (f->bit_size == 0) {
        REQUIRE(value == 0);
        return;
    }
    REQUIRE(f->bit_size <= 32);
    if (f->bit_size == 32) {
        return;
    }
    int64_t span = (int64_t)1 << f->bit_size;
    if (f->is_signed) {
        REQUIRE(value >= -span / 2 && value < span / 2);
    } else {
        REQUIRE(value >= 0 && value < span);
    }
}

static void check_forwarding(const mouse_report_t *r, uint8_t fail_bits) {
    // Everything works: the computer sees the buttons and the whole (clamped) move.
    mouse_forward_t state = {0};
    sink_t sink = {0};
    mouse_forward(&state, true, r, sink_send, &sink);
    REQUIRE(state.sent == r->buttons);
    REQUIRE(sink.last_buttons == r->buttons);
    REQUIRE(sink.x == bridge_clamp(r->x, BRIDGE_MAX_MOVE));
    REQUIRE(sink.y == bridge_clamp(r->y, BRIDGE_MAX_MOVE));
    REQUIRE(sink.wheel == bridge_clamp(r->wheel, BRIDGE_MAX_MOVE));

    // Sends fail, then recover: the buttons must still converge. A lost release
    // would leave a click stuck on the computer.
    mouse_forward_t flaky = {0};
    sink_t flaky_sink = {.failing = true, .fail_bits = fail_bits};
    mouse_forward(&flaky, true, r, sink_send, &flaky_sink);
    flaky_sink.failing = false;
    mouse_forward(&flaky, true, NULL, sink_send, &flaky_sink);
    REQUIRE(flaky.sent == r->buttons);

    // Release: nothing stays held.
    const mouse_report_t idle = {0};
    flaky_sink.failing = true;
    mouse_forward(&flaky, true, &idle, sink_send, &flaky_sink);
    flaky_sink.failing = false;
    mouse_forward(&flaky, true, NULL, sink_send, &flaky_sink);
    REQUIRE(flaky.sent == 0);
    if (r->buttons != 0) {
        REQUIRE(flaky_sink.last_buttons == 0);
    }

    // Unplugged: nothing is sent, and the next computer starts clean.
    uint32_t calls = flaky_sink.calls;
    mouse_forward(&flaky, false, r, sink_send, &flaky_sink);
    REQUIRE(flaky_sink.calls == calls);
    REQUIRE(flaky.sent == 0);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2) {
        return 0;
    }
    size_t rest = size - 2;
    size_t map_len = ((size_t)data[0] | ((size_t)data[1] << 8)) % (rest + 1);
    size_t report_len = rest - map_len;

    uint8_t *map = malloc(map_len ? map_len : 1);
    uint8_t *report = malloc(report_len ? report_len : 1);
    REQUIRE(map != NULL && report != NULL);
    memcpy(map, data + 2, map_len);
    memcpy(report, data + 2 + map_len, report_len);
    uint8_t fail_bits = data[size - 1];

    mouse_layout_t layouts[MAX_LAYOUTS];
    size_t count = mouse_parse_report_map(map, map_len, 0, layouts, MAX_LAYOUTS);
    REQUIRE(count <= MAX_LAYOUTS);

    for (size_t i = 0; i < count; i++) {
        const mouse_layout_t *layout = &layouts[i];
        REQUIRE(layout->x.bit_size != 0 && layout->y.bit_size != 0);
        REQUIRE(mouse_layout_find(layouts, count, 0, layout->report_id) == layout);
        REQUIRE(mouse_layout_find(layouts, count, 1, layout->report_id) == NULL);

        mouse_report_t r;
        mouse_decode(layout, report, report_len, &r);
        REQUIRE((r.buttons & ~0x1Fu) == 0);
        check_field(&layout->x, r.x);
        check_field(&layout->y, r.y);
        check_field(&layout->wheel, r.wheel);
        check_forwarding(&r, fail_bits);
    }

    mouse_report_t boot;
    if (mouse_decode_boot(report, report_len, &boot)) {
        REQUIRE((boot.buttons & ~0x1Fu) == 0);
        check_forwarding(&boot, fail_bits);
    }

    free(map);
    free(report);
    return 0;
}
