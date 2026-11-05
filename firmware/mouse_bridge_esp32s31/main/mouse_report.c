// mouse_report.c: see mouse_report.h. Item encoding is from the USB HID 1.11 spec, section 6.2.2.

#include "mouse_report.h"

#include <string.h>

#include "bridge_logic.h"

#define PAGE_GENERIC_DESKTOP 0x01u
#define PAGE_BUTTON 0x09u
#define USAGE_X 0x30u
#define USAGE_Y 0x31u
#define USAGE_WHEEL 0x38u

#define ITEM_MAIN 0
#define ITEM_GLOBAL 1
#define ITEM_LOCAL 2
#define ITEM_LONG_PREFIX 0xFE

#define INPUT_CONSTANT 0x01u
#define INPUT_VARIABLE 0x02u
#define INPUT_RELATIVE 0x04u

#define MAX_REPORTS 16        // report IDs tracked per map
#define MAX_USAGES 16         // local usages tracked per main item
#define MAX_GLOBAL_STACK 4    // nested Push items
#define MAX_REPORT_BITS 65536u  // far beyond any real mouse; stops hostile sizes

typedef struct {
    uint32_t usage_page;
    int32_t logical_min;
    uint32_t report_size;
    uint32_t report_count;
    uint8_t report_id;
} globals_t;

typedef struct {
    uint32_t usages[MAX_USAGES];
    size_t usage_count;
    uint32_t usage_min, usage_max;
    bool has_range;
} locals_t;

typedef struct {
    uint32_t bits;
    mouse_layout_t layout;
} report_slot_t;

static report_slot_t *slot_for(report_slot_t *slots, size_t *count, uint8_t id, uint8_t map_index) {
    for (size_t i = 0; i < *count; i++) {
        if (slots[i].layout.report_id == id) {
            return &slots[i];
        }
    }
    if (*count == MAX_REPORTS) {
        return NULL;
    }
    report_slot_t *s = &slots[(*count)++];
    memset(s, 0, sizeof(*s));
    s->layout.map_index = map_index;
    s->layout.report_id = id;
    for (int i = 0; i < MOUSE_MAX_BUTTONS; i++) {
        s->layout.button_bit[i] = -1;
    }
    return s;
}

// Per the spec, extra fields reuse the last listed usage.
static uint32_t usage_for_field(const locals_t *l, uint32_t index) {
    if (l->usage_count > 0) {
        return l->usages[index < l->usage_count ? index : l->usage_count - 1];
    }
    if (l->has_range) {
        uint32_t u = l->usage_min + index;
        return u <= l->usage_max ? u : l->usage_max;
    }
    return 0;
}

static void set_field(mouse_field_t *f, uint32_t offset, const globals_t *g) {
    if (f->bit_size != 0) {
        return;  // first match wins
    }
    f->bit_offset = offset;
    f->bit_size = (uint8_t)g->report_size;
    f->is_signed = g->logical_min < 0;
}

static void add_field(mouse_layout_t *m, uint32_t usage, uint32_t offset, uint32_t flags,
                      const globals_t *g) {
    uint32_t page = usage >> 16;
    uint32_t id = usage & 0xFFFFu;
    if (page == PAGE_BUTTON) {
        if (id >= 1 && id <= MOUSE_MAX_BUTTONS && g->report_size == 1 &&
            m->button_bit[id - 1] < 0) {
            m->button_bit[id - 1] = (int32_t)offset;
        }
    } else if (page == PAGE_GENERIC_DESKTOP && (flags & INPUT_RELATIVE)) {
        if (id == USAGE_X) {
            set_field(&m->x, offset, g);
        } else if (id == USAGE_Y) {
            set_field(&m->y, offset, g);
        } else if (id == USAGE_WHEEL) {
            set_field(&m->wheel, offset, g);
        }
    }
}

// Returns false once sizes stop being believable, which ends the parse.
static bool handle_input(report_slot_t *slot, uint32_t flags, const globals_t *g,
                         const locals_t *l) {
    if (g->report_size > 32 || g->report_count > MAX_REPORT_BITS) {
        return false;
    }
    uint32_t total = g->report_size * g->report_count;
    if (total > MAX_REPORT_BITS || slot->bits > MAX_REPORT_BITS - total) {
        return false;
    }
    if (!(flags & INPUT_CONSTANT) && (flags & INPUT_VARIABLE) && g->report_size > 0) {
        for (uint32_t i = 0; i < g->report_count; i++) {
            add_field(&slot->layout, usage_for_field(l, i), slot->bits + i * g->report_size, flags,
                      g);
        }
    }
    slot->bits += total;
    return true;
}

size_t mouse_parse_report_map(const uint8_t *map, size_t map_len, uint8_t map_index,
                              mouse_layout_t *layouts, size_t max_layouts) {
    report_slot_t slots[MAX_REPORTS];
    size_t slot_count = 0;
    globals_t g = {0};
    globals_t stack[MAX_GLOBAL_STACK];
    size_t depth = 0;
    locals_t l = {0};

    size_t pos = 0;
    while (pos < map_len) {
        uint8_t prefix = map[pos++];
        if (prefix == ITEM_LONG_PREFIX) {
            if (pos >= map_len) {
                break;
            }
            pos += (size_t)2 + map[pos];  // data size byte, tag byte, data
            continue;
        }

        size_t size = prefix & 0x03u;
        if (size == 3) {
            size = 4;
        }
        if (size > map_len - pos) {
            break;
        }
        uint32_t value = 0;
        for (size_t i = 0; i < size; i++) {
            value |= (uint32_t)map[pos + i] << (8 * i);
        }
        int32_t signed_value = (int32_t)value;
        if (size == 1) {
            signed_value = (int8_t)value;
        } else if (size == 2) {
            signed_value = (int16_t)value;
        }
        pos += size;

        uint8_t type = (prefix >> 2) & 0x03u;
        uint8_t tag = prefix >> 4;
        // A 4-byte usage carries its own page in the high 16 bits.
        uint32_t usage = size == 4 ? value : (g.usage_page << 16) | (value & 0xFFFFu);

        if (type == ITEM_MAIN) {
            if (tag == 0x8) {  // Input
                report_slot_t *slot = slot_for(slots, &slot_count, g.report_id, map_index);
                if (slot == NULL || !handle_input(slot, value, &g, &l)) {
                    break;
                }
            }
            memset(&l, 0, sizeof(l));  // every main item clears local state
        } else if (type == ITEM_GLOBAL) {
            switch (tag) {
                case 0x0:
                    g.usage_page = value & 0xFFFFu;
                    break;
                case 0x1:
                    g.logical_min = signed_value;
                    break;
                case 0x7:
                    g.report_size = value;
                    break;
                case 0x8:
                    g.report_id = (uint8_t)value;
                    break;
                case 0x9:
                    g.report_count = value;
                    break;
                case 0xA:  // Push
                    if (depth < MAX_GLOBAL_STACK) {
                        stack[depth++] = g;
                    }
                    break;
                case 0xB:  // Pop
                    if (depth > 0) {
                        g = stack[--depth];
                    }
                    break;
                default:
                    break;
            }
        } else if (type == ITEM_LOCAL) {
            if (tag == 0x0) {
                if (l.usage_count < MAX_USAGES) {
                    l.usages[l.usage_count++] = usage;
                }
            } else if (tag == 0x1) {
                l.usage_min = usage;
                l.has_range = true;
            } else if (tag == 0x2) {
                l.usage_max = usage;
            }
        }
    }

    size_t written = 0;
    for (size_t i = 0; i < slot_count && written < max_layouts; i++) {
        const mouse_layout_t *m = &slots[i].layout;
        if (m->x.bit_size != 0 && m->y.bit_size != 0) {
            layouts[written++] = *m;
        }
    }
    return written;
}

const mouse_layout_t *mouse_layout_find(const mouse_layout_t *layouts, size_t count,
                                        uint8_t map_index, uint8_t report_id) {
    for (size_t i = 0; i < count; i++) {
        if (layouts[i].map_index == map_index && layouts[i].report_id == report_id) {
            return &layouts[i];
        }
    }
    return NULL;
}

static uint32_t read_bits(const uint8_t *data, size_t len, uint32_t offset, uint8_t size) {
    if (size == 0 || size > 32 || offset / 8 >= len || (offset + size - 1u) / 8 >= len) {
        return 0;
    }
    uint32_t value = 0;
    for (uint8_t i = 0; i < size; i++) {
        uint32_t bit = offset + i;
        if (data[bit / 8] & (1u << (bit % 8))) {
            value |= (uint32_t)1 << i;
        }
    }
    return value;
}

static int32_t read_field(const mouse_field_t *f, const uint8_t *data, size_t len) {
    uint32_t value = read_bits(data, len, f->bit_offset, f->bit_size);
    if (f->is_signed && f->bit_size > 0 && f->bit_size < 32 &&
        (value & ((uint32_t)1 << (f->bit_size - 1)))) {
        value |= ~(uint32_t)0 << f->bit_size;
    }
    return (int32_t)value;
}

void mouse_decode(const mouse_layout_t *layout, const uint8_t *data, size_t len,
                  mouse_report_t *out) {
    out->x = read_field(&layout->x, data, len);
    out->y = read_field(&layout->y, data, len);
    out->wheel = read_field(&layout->wheel, data, len);
    out->buttons = 0;
    for (int i = 0; i < MOUSE_MAX_BUTTONS; i++) {
        int32_t bit = layout->button_bit[i];
        if (bit >= 0 && read_bits(data, len, (uint32_t)bit, 1)) {
            out->buttons |= (uint8_t)(1u << i);
        }
    }
}

bool mouse_decode_boot(const uint8_t *data, size_t len, mouse_report_t *out) {
    if (len < 3) {
        return false;
    }
    out->buttons = data[0] & 0x1Fu;
    out->x = (int8_t)data[1];
    out->y = (int8_t)data[2];
    out->wheel = 0;
    return true;
}

void mouse_forward(mouse_forward_t *state, bool mounted, const mouse_report_t *report,
                   mouse_send_fn send, void *ctx) {
    if (report != NULL) {
        state->want = report->buttons;
    }
    if (!mounted) {
        state->sent = 0;  // the next computer starts with nothing held
        return;
    }
    if (state->want != state->sent) {
        if (!send(ctx, state->want, 0, 0, 0)) {
            return;
        }
        state->sent = state->want;
    }
    if (report == NULL) {
        return;
    }
    bridge_motion_t motion = bridge_motion_make(report->x, report->y, report->wheel);
    int8_t x, y, wheel;
    while (bridge_motion_next(&motion, &x, &y, &wheel)) {
        if (!send(ctx, state->sent, x, y, wheel)) {
            return;
        }
    }
}
