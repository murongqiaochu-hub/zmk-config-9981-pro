/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "../config/boards/arm/bbp9981/custom_driver/gamepad_report.h"

_Static_assert(CHAR_BIT == 8, "HID byte width");
_Static_assert(BBP_GAMEPAD_REPORT_ID == 4 && BBP_GAMEPAD_LAYER == 3, "Report/layer ABI");
_Static_assert(BBP_GAMEPAD_POSITIONS == 42 && BBP_GAMEPAD_ACTION_COUNT == 19, "Owner ABI");
_Static_assert(BBP_GP_UP == 0 && BBP_GP_DOWN == 1 && BBP_GP_LEFT == 2 && BBP_GP_RIGHT == 3,
               "Direction action ABI");
_Static_assert(BBP_GP_A == 4 && BBP_GP_B == 5 && BBP_GP_X == 6 && BBP_GP_Y == 7 &&
               BBP_GP_L == 8 && BBP_GP_R == 9 && BBP_GP_LT == 10 && BBP_GP_RT == 11 &&
               BBP_GP_SELECT == 12 && BBP_GP_START == 13 && BBP_GP_GUIDE == 14 &&
               BBP_GP_L3 == 15 && BBP_GP_R3 == 16 && BBP_GP_RIGHT_STICK_MOD == 17 &&
               BBP_GP_STICK_CLICK == 18, "Xbox-layout action ABI");
_Static_assert(BBP_GP_LB == 8 && BBP_GP_RB == 9, "Legacy shoulder aliases");
_Static_assert(sizeof(struct bbp_gamepad_report_body) == 9 &&
               sizeof(struct bbp_gamepad_report) == 10, "9 body / 10 USB bytes");
_Static_assert(offsetof(struct bbp_gamepad_report, body) == 1 &&
               offsetof(struct bbp_gamepad_report_body, buttons_lo) == 0 &&
               offsetof(struct bbp_gamepad_report_body, buttons_hi) == 1 &&
               offsetof(struct bbp_gamepad_report_body, hat) == 2 &&
               offsetof(struct bbp_gamepad_report_body, x) == 3 &&
               offsetof(struct bbp_gamepad_report_body, y) == 4 &&
               offsetof(struct bbp_gamepad_report_body, z) == 5 &&
               offsetof(struct bbp_gamepad_report_body, rz) == 6 &&
               offsetof(struct bbp_gamepad_report_body, lt) == 7 &&
               offsetof(struct bbp_gamepad_report_body, rt) == 8, "Wire field offsets");
_Static_assert(BBP_GAMEPAD_STICK_GAIN == 8 && BBP_GAMEPAD_STICK_DEADZONE == 4 &&
               BBP_GAMEPAD_STICK_QUIET_MS == 120, "Initial velocity parameters");

static const uint8_t descriptor[] = {BBP_GAMEPAD_DESCRIPTOR_BYTES};
/* Independent expected HID usage order, not derived from production tables. */
static const uint8_t button_usages[13] = {1,2,4,5,7,8,9,10,11,12,13,14,15};
static uint32_t random_state = 0x9981U;
static uint32_t next(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}
static uint16_t buttons_of(struct bbp_gamepad_report_body r) {
    return (uint16_t)(r.buttons_lo | ((uint16_t)r.buttons_hi << 8));
}
static void axes(const struct bbp_gamepad_state *s, int x, int y, int z, int rz) {
    struct bbp_gamepad_report_body r = bbp_gamepad_snapshot(s);
    assert(r.x == x && r.y == y && r.z == z && r.rz == rz);
}
static struct bbp_gamepad_report_body reference(const uint8_t *owners) {
    bool a[19] = {false};
    for (unsigned p = 0; p < 42; ++p) {
        assert(owners[p] <= 19);
        if (owners[p]) { a[owners[p] - 1] = true; }
    }
    uint16_t bits = 0;
    for (unsigned b = 0; b < 13; ++b) {
        if (a[b + 4]) { bits |= (uint16_t)(1U << (button_usages[b] - 1)); }
    }
    int x = (int)a[3] - (int)a[2], y = (int)a[1] - (int)a[0];
    static const uint8_t hats[3][3] = {{7,0,1},{6,8,2},{5,4,3}};
    return (struct bbp_gamepad_report_body){.buttons_lo = (uint8_t)bits,
        .buttons_hi = (uint8_t)(bits >> 8), .hat = hats[y + 1][x + 1],
        .lt = a[10] ? 255 : 0, .rt = a[11] ? 255 : 0};
}

/* Parse short HID items, then compare every field against a fixed external
 * contract by wire offset. In particular, do NOT infer the expected usage set
 * or bit length from the descriptor under test. Poisoned globals also catch
 * accidental dependence on a preceding keyboard/mouse descriptor. */
struct hid_globals {
    uint32_t page, size, count, report, unit;
    int32_t lo, hi, physical_lo, physical_hi, exponent;
};
static int32_t signed_value(uint32_t value, unsigned length) {
    assert(length == 1 || length == 2 || length == 4);
    unsigned bits = length * 8;
    int64_t wide = value;
    if ((uint64_t)value & (UINT64_C(1) << (bits - 1))) { wide -= INT64_C(1) << bits; }
    return (int32_t)wide;
}
static void expected_field(unsigned offset, const struct hid_globals *g,
                           uint32_t usage, uint32_t flags) {
    static const uint8_t by_button_bit[16] = {1,2,0,4,5,0,7,8,9,10,11,12,13,14,15,0};
    assert(g->report == 4);
    if (offset < 16) {
        assert(g->page == 9 && g->size == 1 && g->lo == 0 && g->hi == 1);
        assert(g->physical_lo == 0 && g->physical_hi == 0 && g->unit == 0 && g->exponent == 0);
        assert(flags == (by_button_bit[offset] ? 0x02U : 0x03U));
        assert(usage == by_button_bit[offset]);
    } else if (offset == 16 || offset == 20) {
        assert(g->page == 1 && g->size == 4 && g->lo == 0 && g->hi == 7);
        assert(g->physical_lo == 0 && g->physical_hi == 315 && g->unit == 0x14 && g->exponent == 0);
        assert(usage == (offset == 16 ? 0x39U : 0U));
        assert(flags == (offset == 16 ? 0x42U : 0x03U)); /* null hat 8 + constant nibble */
    } else if (offset >= 24 && offset < 56) {
        static const uint8_t expected_axes[4] = {0x30,0x31,0x32,0x35};
        assert((offset - 24) % 8 == 0 && g->page == 1 && g->size == 8);
        assert(usage == expected_axes[(offset - 24) / 8] && flags == 0x02);
        assert(g->lo == -127 && g->hi == 127 && g->physical_lo == 0 && g->physical_hi == 0);
        assert(g->unit == 0 && g->exponent == 0);
    } else {
        assert(offset == 56 || offset == 64);
        assert(g->page == 2 && g->size == 8 && flags == 0x02);
        assert(usage == (offset == 56 ? 0xc5U : 0xc4U)); /* Brake LT, Accelerator RT */
        assert(g->lo == 0 && g->hi == 255 && g->physical_lo == 0 && g->physical_hi == 0);
        assert(g->unit == 0 && g->exponent == 0);
    }
}
static void test_descriptor(void) {
    struct hid_globals g = {.page = 0x77, .size = 32, .count = 17, .report = 2,
        .unit = 0x99, .lo = -61, .hi = 90, .physical_lo = -71, .physical_hi = 80, .exponent = -3};
    uint32_t usages[32] = {0}, usage_min = 0, usage_max = 0;
    unsigned usage_count = 0, offset = 0, fields = 0, real_buttons = 0, constant_buttons = 0;
    uint32_t seen_button_usages = 0;
    bool have_min = false, have_max = false;
    unsigned depth = 0, collections = 0;
    size_t pos = 0;
    while (pos < sizeof(descriptor)) {
        uint8_t prefix = descriptor[pos++];
        assert(prefix != 0xfe);
        static const unsigned lengths[4] = {0,1,2,4};
        unsigned length = lengths[prefix & 3U], type = (prefix >> 2) & 3U, tag = prefix >> 4;
        assert(pos + length <= sizeof(descriptor));
        uint32_t value = 0;
        for (unsigned i = 0; i < length; ++i) { value |= (uint32_t)descriptor[pos++] << (8 * i); }
        if (type == 1) {
            switch (tag) {
            case 0: g.page = value; break;
            case 1: g.lo = signed_value(value, length); break;
            case 2: g.hi = g.lo < 0 ? signed_value(value, length) : (int32_t)value; break;
            case 3: g.physical_lo = signed_value(value, length); break;
            case 4: g.physical_hi = g.physical_lo < 0 ? signed_value(value, length) : (int32_t)value; break;
            case 5: g.exponent = signed_value(value, length); break;
            case 6: g.unit = value; break;
            case 7: g.size = value; break;
            case 8: assert(value == 4); g.report = value; break;
            case 9: g.count = value; break;
            default: assert(0 && "Unexpected HID global item");
            }
        } else if (type == 2) {
            if (tag == 0) { assert(usage_count < 32); usages[usage_count++] = value; }
            else if (tag == 1) { usage_min = value; have_min = true; }
            else if (tag == 2) { usage_max = value; have_max = true; }
            else { assert(0 && "Unexpected HID local item"); }
        } else {
            assert(type == 0);
            if (tag == 10) {
                assert(depth == 0 && value == 1 && g.page == 1 && usage_count == 1 && usages[0] == 5);
                ++depth; ++collections;
            } else if (tag == 12) {
                assert(depth == 1); --depth;
            } else {
                assert(tag == 8 && depth == 1); /* no Output/Feature (including rumble) */
                assert(g.size > 0 && g.size <= 8 && g.count > 0 && g.count <= 16);
                assert(have_min == have_max);
                for (uint32_t i = 0; i < g.count; ++i) {
                    uint32_t usage = 0;
                    if (have_min) { assert(usage_min + i <= usage_max); usage = usage_min + i; }
                    else if (i < usage_count) { usage = usages[i]; }
                    expected_field(offset, &g, usage, value);
                    if (g.page == 9) {
                        if (value & 1U) { ++constant_buttons; }
                        else {
                            assert(usage >= 1 && usage <= 16);
                            seen_button_usages |= UINT32_C(1) << (usage - 1);
                            ++real_buttons;
                        }
                    }
                    offset += g.size;
                    ++fields;
                }
            }
            usage_count = 0; have_min = have_max = false;
        }
    }
    assert(depth == 0 && collections == 1 && offset == 72 && fields == 24);
    assert(real_buttons == 13 && constant_buttons == 3 && seen_button_usages == 0x7fdbU);
}

static void test_neutral(void) {
    struct bbp_gamepad_report_body r = bbp_gamepad_neutral();
    const uint8_t expected[9] = {0,0,8,0,0,0,0,0,0};
    assert(!memcmp(&r, expected, 9) && bbp_gamepad_is_neutral(&r));
    assert(!bbp_gamepad_is_neutral(NULL));
    for (unsigned field = 0; field < 9; ++field) {
        uint8_t changed[9]; memcpy(changed, expected, 9);
        changed[field] = field == 2 ? 0 : 1;
        memcpy(&r, changed, 9);
        assert(!bbp_gamepad_is_neutral(&r)); /* includes each trigger and all four axes */
    }
    bbp_gamepad_reset(NULL);
    r = bbp_gamepad_snapshot(NULL); assert(bbp_gamepad_is_neutral(&r));
    assert(!bbp_gamepad_key(NULL, 0, 0, true));
    assert(!bbp_gamepad_motion(NULL, 1, 1, 0) && !bbp_gamepad_expire(NULL, 0));
    assert(!bbp_gamepad_motion_cancel(NULL) && bbp_gamepad_motion_remaining_ms(NULL, 0) == 0);
}
static void test_all_button_dpad_states(void) {
    struct bbp_gamepad_state s = {0};
    for (unsigned buttons = 0; buttons < 8192; ++buttons) {
        for (unsigned directions = 0; directions < 16; ++directions) {
            uint8_t model[42] = {0};
            bbp_gamepad_reset(&s);
            for (unsigned a = 0; a < 4; ++a) {
                if (directions & (1U << a)) {
                    assert(bbp_gamepad_key(&s, a, a, true)); model[a] = (uint8_t)(a + 1);
                }
            }
            for (unsigned a = 4; a < 17; ++a) {
                if (buttons & (1U << (a - 4))) {
                    assert(bbp_gamepad_key(&s, a, a, true)); model[a] = (uint8_t)(a + 1);
                }
            }
            struct bbp_gamepad_report_body r = bbp_gamepad_snapshot(&s), expected = reference(model);
            assert(!memcmp(&r, &expected, 9));
            assert(!(buttons_of(r) & 0x8024U) && !(r.hat & 0xf0U));
            assert(!r.x && !r.y && !r.z && !r.rz);
        }
    }
}
static void test_owners_and_triggers(void) {
    struct bbp_gamepad_state s = {0};
    assert(bbp_gamepad_key(&s, 1, BBP_GP_A, true));
    assert(!bbp_gamepad_key(&s, 1, BBP_GP_B, true)); /* repeated DOWN cannot remap */
    assert(bbp_gamepad_key(&s, 2, BBP_GP_A, true));
    assert(bbp_gamepad_key(&s, 1, BBP_GP_B, false)); /* captured owner, not argument */
    assert(buttons_of(bbp_gamepad_snapshot(&s)) == 1);
    assert(bbp_gamepad_key(&s, 2, UINT32_MAX, false));
    struct bbp_gamepad_report_body r = bbp_gamepad_snapshot(&s); assert(bbp_gamepad_is_neutral(&r));
    assert(!bbp_gamepad_key(&s, 42, BBP_GP_A, true));
    assert(!bbp_gamepad_key(&s, UINT32_MAX, BBP_GP_A, false));
    assert(!bbp_gamepad_key(&s, 0, 19, true) && !bbp_gamepad_key(&s, 0, 0, false));
    for (unsigned a = 4; a < 17; ++a) {
        assert(bbp_gamepad_key(&s, 0, a, true)); r = bbp_gamepad_snapshot(&s);
        assert(buttons_of(r) == (1U << (button_usages[a - 4] - 1)));
        assert(r.lt == (a == 10 ? 255 : 0) && r.rt == (a == 11 ? 255 : 0));
        assert(bbp_gamepad_key(&s, 0, UINT32_MAX, false));
        r = bbp_gamepad_snapshot(&s); assert(bbp_gamepad_is_neutral(&r));
    }
    assert(bbp_gamepad_key(&s, 0, BBP_GP_LT, true));
    assert(bbp_gamepad_key(&s, 1, BBP_GP_LT, true));
    assert(bbp_gamepad_key(&s, 2, BBP_GP_RT, true));
    r = bbp_gamepad_snapshot(&s); assert(buttons_of(r) == 0x300 && r.lt == 255 && r.rt == 255);
    assert(bbp_gamepad_key(&s, 0, BBP_GP_RT, false));
    r = bbp_gamepad_snapshot(&s); assert(buttons_of(r) == 0x300 && r.lt == 255 && r.rt == 255);
    assert(bbp_gamepad_key(&s, 1, BBP_GP_RT, false));
    r = bbp_gamepad_snapshot(&s); assert(buttons_of(r) == 0x200 && !r.lt && r.rt == 255);
    assert(bbp_gamepad_key(&s, 2, BBP_GP_LT, false));
    r = bbp_gamepad_snapshot(&s); assert(bbp_gamepad_is_neutral(&r));
}
static bool model_edge(uint8_t *model, unsigned p, unsigned a, bool down) {
    if (p >= 42) { return false; }
    if (!down) { bool changed = model[p] != 0; model[p] = 0; return changed; }
    if (a >= 19 || model[p]) { return false; }
    if (a == 18) {
        a = 15;
        for (unsigned i = 0; i < 42; ++i) { if (model[i] == 18) { a = 16; } }
    }
    model[p] = (uint8_t)(a + 1);
    return true;
}
static void test_modeled_edges(void) {
    struct bbp_gamepad_state s = {0};
    uint8_t model[42] = {0};
    for (unsigned i = 0; i < 250000; ++i) {
        unsigned p = next() % 44, a = next() % 21;
        bool down = (next() & 1U) != 0;
        bool changed = model_edge(model, p, a, down);
        assert(bbp_gamepad_key(&s, p, a, down) == changed);
        assert(!memcmp(s.owners, model, 42));
        struct bbp_gamepad_report_body r = bbp_gamepad_snapshot(&s), expected = reference(model);
        assert(!memcmp(&r, &expected, 9));
        if (i % 4096 == 0) { bbp_gamepad_reset(&s); memset(model, 0, 42); }
    }
}
static void test_click_mode_and_cancel(void) {
    struct bbp_gamepad_state s = {0};
    assert(bbp_gamepad_key(&s, 0, BBP_GP_UP, true));
    assert(bbp_gamepad_key(&s, 1, BBP_GP_A, true));
    assert(bbp_gamepad_key(&s, 2, BBP_GP_LT, true));
    assert(bbp_gamepad_motion(&s, 8, -8, 100)); axes(&s, 48, -48, 0, 0);
    assert(bbp_gamepad_key(&s, 3, BBP_GP_STICK_CLICK, true));
    assert(s.owners[3] == 16 && buttons_of(bbp_gamepad_snapshot(&s)) == 0x2101);
    assert(bbp_gamepad_key(&s, 4, BBP_GP_RIGHT_STICK_MOD, true));
    axes(&s, 0, 0, 0, 0); assert(bbp_gamepad_motion_remaining_ms(&s, 100) == 0);
    struct bbp_gamepad_report_body r = bbp_gamepad_snapshot(&s);
    assert(buttons_of(r) == 0x2101 && r.hat == 0 && r.lt == 255 && !r.rt);
    assert(s.owners[3] == 16); /* existing click remains L3 */
    assert(bbp_gamepad_motion(&s, -16, 16, 200)); axes(&s, 0, 0, -95, 95);
    assert(!bbp_gamepad_key(&s, 4, BBP_GP_RIGHT_STICK_MOD, true));
    axes(&s, 0, 0, -95, 95); /* duplicate BB must not centre */
    assert(bbp_gamepad_key(&s, 6, BBP_GP_RIGHT_STICK_MOD, true));
    axes(&s, 0, 0, -95, 95); /* a second BB owner must not centre */
    assert(bbp_gamepad_key(&s, 4, BBP_GP_STICK_CLICK, false));
    axes(&s, 0, 0, -95, 95); /* one BB remains */
    assert(bbp_gamepad_key(&s, 5, BBP_GP_STICK_CLICK, true));
    assert(s.owners[5] == 17 && buttons_of(bbp_gamepad_snapshot(&s)) == 0x6101);
    assert(!bbp_gamepad_key(&s, 5, BBP_GP_L3, true));
    assert(bbp_gamepad_key(&s, 6, BBP_GP_A, false));
    axes(&s, 0, 0, 0, 0); assert(s.owners[5] == 17); /* click remains R3 */
    assert(bbp_gamepad_motion(&s, 1, 1, 201)); axes(&s, 6, 6, 0, 0); /* no right delta replay */
    assert(bbp_gamepad_key(&s, 3, BBP_GP_RIGHT_STICK_MOD, false));
    assert(buttons_of(bbp_gamepad_snapshot(&s)) == 0x4101);
    assert(bbp_gamepad_key(&s, 5, BBP_GP_A, false));
    r = bbp_gamepad_snapshot(&s); assert(buttons_of(r) == 0x101 && r.hat == 0 && r.lt == 255);
    assert(bbp_gamepad_key(&s, 4, BBP_GP_RIGHT_STICK_MOD, true));
    assert(bbp_gamepad_key(&s, 5, BBP_GP_STICK_CLICK, true));
    assert(bbp_gamepad_motion(&s, 8, 8, 300));
    uint8_t captured[42]; memcpy(captured, s.owners, 42);
    assert(bbp_gamepad_motion_cancel(&s)); axes(&s, 0, 0, 0, 0);
    assert(!memcmp(captured, s.owners, 42) && bbp_gamepad_motion_remaining_ms(&s, 300) == 0);
    assert(!bbp_gamepad_motion_cancel(&s));
    assert(bbp_gamepad_motion(&s, 1, -1, 0)); axes(&s, 0, 0, 6, -6); /* new clock epoch */
    assert(bbp_gamepad_expire(&s, 120));
    assert(!memcmp(captured, s.owners, 42)); /* expiry also preserves all key owners */
    r = bbp_gamepad_snapshot(&s); assert(buttons_of(r) == 0x4101 && r.hat == 0 && r.lt == 255);
    bbp_gamepad_reset(&s); r = bbp_gamepad_snapshot(&s);
    assert(bbp_gamepad_is_neutral(&r) && !bbp_gamepad_right_stick_mode(&s));
}
static void test_keyboard_l3r3_and_guide_owners(void) {
    struct bbp_gamepad_state s = {0};
    struct bbp_gamepad_report_body r;
    /* D=L3 and K=R3 remain independently held while BB changes stick mode. */
    assert(bbp_gamepad_key(&s, 19, BBP_GP_L3, true));
    assert(bbp_gamepad_key(&s, 24, BBP_GP_R3, true));
    assert(bbp_gamepad_key(&s, 3, BBP_GP_RIGHT_STICK_MOD, true));
    /* Trackpad P4 click and Space P39 are both fixed Guide owners, never L3/R3. */
    assert(bbp_gamepad_key(&s, 4, BBP_GP_GUIDE, true));
    assert(bbp_gamepad_key(&s, 39, BBP_GP_GUIDE, true));
    r = bbp_gamepad_snapshot(&s); assert(buttons_of(r) == 0x7000);
    assert(bbp_gamepad_key(&s, 3, BBP_GP_RIGHT_STICK_MOD, false));
    r = bbp_gamepad_snapshot(&s); assert(buttons_of(r) == 0x7000);
    assert(bbp_gamepad_key(&s, 19, BBP_GP_L3, false));
    r = bbp_gamepad_snapshot(&s); assert(buttons_of(r) == 0x5000);
    assert(bbp_gamepad_key(&s, 24, BBP_GP_R3, false));
    r = bbp_gamepad_snapshot(&s); assert(buttons_of(r) == 0x1000);
    assert(bbp_gamepad_key(&s, 4, BBP_GP_GUIDE, false));
    r = bbp_gamepad_snapshot(&s); assert(buttons_of(r) == 0x1000);
    assert(bbp_gamepad_key(&s, 39, BBP_GP_GUIDE, false));
    r = bbp_gamepad_snapshot(&s); assert(bbp_gamepad_is_neutral(&r));
}
static void test_stick_math_and_noise(void) {
    assert(bbp_gamepad_stick_scale(0) == 0 && bbp_gamepad_stick_scale(1) == 8);
    assert(bbp_gamepad_stick_scale(-1) == -8);
    assert(bbp_gamepad_stick_scale(INT32_MAX) == 127 && bbp_gamepad_stick_scale(INT32_MIN) == -127);
    assert(bbp_gamepad_stick_limit(INT64_MAX) == 127 && bbp_gamepad_stick_limit(INT64_MIN) == -127);
    for (int logical = -4; logical <= 4; ++logical) { assert(bbp_gamepad_stick_limit(logical) == 0); }
    assert(bbp_gamepad_stick_limit(5) == 5 && bbp_gamepad_stick_limit(-5) == -5);
    assert(bbp_gamepad_stick_filter(0, 10) == 8 && bbp_gamepad_stick_filter(0, -10) == -8);
    assert(bbp_gamepad_stick_filter(127, -8) == -6 && bbp_gamepad_stick_filter(-127, 8) == 6);
    for (int old = -127; old <= 127; ++old) {
        for (int sample = -127; sample <= 127; ++sample) {
            int value = bbp_gamepad_stick_filter((int8_t)old, (int8_t)sample);
            assert(value >= -127 && value <= 127);
            assert(value == -bbp_gamepad_stick_filter((int8_t)-old, (int8_t)-sample));
        }
    }
    /* Integer raw counts with gain8 step over the logical deadzone. Exercise
     * nonzero sub-deadzone noise at the independent logical-domain boundary. */
    struct bbp_gamepad_stick_state stick = {0};
    assert(bbp_gamepad_stick_update(&stick, 64, -64, 100));
    int64_t now = 101;
    for (int x = -4; x <= 4; ++x) {
        for (int y = -4; y <= 4; ++y) {
            assert(!bbp_gamepad_stick_update(&stick, x, y, now++));
            assert(stick.x == 48 && stick.y == -48);
        }
    }
    assert(bbp_gamepad_stick_remaining_ms(&stick, 219) == 1);
    assert(!bbp_gamepad_stick_expire(&stick, 219));
    assert(bbp_gamepad_stick_expire(&stick, 220) && stick.x == 0 && stick.y == 0);
}
static void test_motion_timing_direction_and_limits(void) {
    struct bbp_gamepad_state s = {0};
    assert(!bbp_gamepad_motion(&s, 0, 0, 0) && bbp_gamepad_motion_remaining_ms(&s, 0) == 0);
    assert(bbp_gamepad_motion(&s, 1, -1, 100)); axes(&s, 6, -6, 0, 0);
    assert(bbp_gamepad_motion_remaining_ms(&s, 100) == 120);
    assert(bbp_gamepad_motion(&s, 1, -1, 101)); axes(&s, 8, -8, 0, 0);
    assert(!bbp_gamepad_motion(&s, 1, -1, 102)); /* same value still refreshes deadline */
    assert(!bbp_gamepad_motion(&s, 0, 0, 150)); axes(&s, 8, -8, 0, 0);
    assert(bbp_gamepad_motion_remaining_ms(&s, 150) == 72);
    assert(!bbp_gamepad_expire(&s, 221) && bbp_gamepad_motion_remaining_ms(&s, 221) == 1);
    assert(bbp_gamepad_expire(&s, 222)); axes(&s, 0, 0, 0, 0);
    assert(!bbp_gamepad_expire(&s, 223) && bbp_gamepad_motion_remaining_ms(&s, 223) == 0);
    bbp_gamepad_reset(&s);
    assert(bbp_gamepad_motion(&s, 1, -1, 0));
    assert(!bbp_gamepad_motion(&s, 0, 0, 119)); axes(&s, 6, -6, 0, 0);
    assert(bbp_gamepad_motion(&s, 0, 0, 120)); axes(&s, 0, 0, 0, 0);
    assert(bbp_gamepad_motion_remaining_ms(&s, 120) == 0); /* quiet packets cannot postpone expiry */
    bbp_gamepad_reset(&s);
    assert(bbp_gamepad_motion(&s, 8, -4, 100)); axes(&s, 48, -24, 0, 0);
    assert(bbp_gamepad_motion(&s, -8, 4, 101)); axes(&s, -48, 24, 0, 0); /* immediate reversal */
    assert(bbp_gamepad_motion(&s, -8, 4, 102)); axes(&s, -60, 30, 0, 0);
    assert(bbp_gamepad_motion(&s, 0, 1, 103)); axes(&s, -15, 14, 0, 0); /* component velocity decay */
    bbp_gamepad_reset(&s);
    const int ramp[4] = {95,119,125,127};
    for (unsigned i = 0; i < 4; ++i) {
        assert(bbp_gamepad_motion(&s, INT32_MAX, INT32_MIN, 100 + i));
        axes(&s, ramp[i], -ramp[i], 0, 0);
    }
    assert(!bbp_gamepad_motion(&s, INT32_MAX, INT32_MIN, 104));
    assert(bbp_gamepad_motion_remaining_ms(&s, 104) == 120);
    assert(bbp_gamepad_key(&s, 41, BBP_GP_RIGHT_STICK_MOD, true)); axes(&s, 0, 0, 0, 0);
    for (unsigned i = 0; i < 4; ++i) {
        assert(bbp_gamepad_motion(&s, INT32_MIN, INT32_MAX, 200 + i));
        axes(&s, 0, 0, -ramp[i], ramp[i]);
    }
    assert(bbp_gamepad_motion(&s, INT32_MAX, INT32_MIN, 204)); axes(&s, 0, 0, 95, -95);
    bbp_gamepad_reset(&s);
    assert(bbp_gamepad_motion(&s, 8, 0, 0)); axes(&s, 48, 0, 0, 0);
    assert(bbp_gamepad_motion(&s, 1, 0, 120)); axes(&s, 6, 0, 0, 0); /* gap expires old filter */
    assert(bbp_gamepad_motion(&s, 8, 0, 100000)); axes(&s, 48, 0, 0, 0); /* no displacement accumulation */
    assert(bbp_gamepad_motion(&s, 8, 0, 100001)); axes(&s, 60, 0, 0, 0);
    bbp_gamepad_reset(&s);
    assert(bbp_gamepad_motion(&s, 1, 0, 0)); axes(&s, 6, 0, 0, 0);
}
static void test_invalid_and_extreme_time(void) {
    struct bbp_gamepad_state s = {0};
    assert(bbp_gamepad_motion(&s, 8, 8, 100));
    assert(!bbp_gamepad_motion(&s, 0, 0, 110));
    assert(bbp_gamepad_motion(&s, 8, 8, 105)); axes(&s, 0, 0, 0, 0); /* backward packet discarded */
    assert(bbp_gamepad_motion_remaining_ms(&s, 105) == 0);
    assert(bbp_gamepad_motion(&s, 8, 8, 106)); axes(&s, 48, 48, 0, 0);
    assert(!bbp_gamepad_expire(&s, 110));
    assert(bbp_gamepad_motion_remaining_ms(&s, 109) == 0); axes(&s, 48, 48, 0, 0); /* const query */
    assert(bbp_gamepad_expire(&s, 109)); axes(&s, 0, 0, 0, 0);
    assert(bbp_gamepad_motion(&s, 8, 8, 110));
    assert(bbp_gamepad_motion(&s, INT32_MAX, INT32_MIN, -1)); axes(&s, 0, 0, 0, 0);
    assert(!bbp_gamepad_motion(&s, 8, 8, INT64_MIN));
    assert(bbp_gamepad_motion(&s, 8, 8, 0));
    assert(bbp_gamepad_motion_remaining_ms(&s, -1) == 0);
    assert(bbp_gamepad_expire(&s, INT64_MIN)); axes(&s, 0, 0, 0, 0);
    assert(bbp_gamepad_motion(&s, 16, -16, INT64_MAX - 120));
    assert(bbp_gamepad_motion_remaining_ms(&s, INT64_MAX - 120) == 120);
    assert(!bbp_gamepad_expire(&s, INT64_MAX - 1));
    assert(bbp_gamepad_motion_remaining_ms(&s, INT64_MAX - 1) == 1);
    assert(bbp_gamepad_expire(&s, INT64_MAX)); axes(&s, 0, 0, 0, 0);
    bbp_gamepad_reset(&s);
    assert(bbp_gamepad_motion(&s, INT32_MAX, INT32_MIN, INT64_MAX));
    assert(bbp_gamepad_motion_remaining_ms(&s, INT64_MAX) == 120 && !bbp_gamepad_expire(&s, INT64_MAX));
    assert(bbp_gamepad_motion(&s, 1, 1, 0)); axes(&s, 0, 0, 0, 0); /* clock rollback failclosed */
    assert(bbp_gamepad_motion(&s, 1, 1, 0)); axes(&s, 6, 6, 0, 0); /* next valid epoch rearms */
    assert(bbp_gamepad_expire(&s, INT64_MAX)); axes(&s, 0, 0, 0, 0); /* huge gap subtraction safe */
}
static void test_motion_stress(void) {
    struct bbp_gamepad_state s = {0};
    int64_t now = 0;
    bool right = false;
    for (unsigned i = 0; i < 32768; ++i) {
        if (i % 97 == 0) {
            right = !right;
            assert(bbp_gamepad_key(&s, 41, BBP_GP_RIGHT_STICK_MOD, right));
            axes(&s, 0, 0, 0, 0);
        }
        int32_t dx = (int32_t)(next() & INT32_MAX), dy = (int32_t)(next() & INT32_MAX);
        if (next() & 1U) { dx = -dx; }
        if (next() & 1U) { dy = -dy; }
        if (i % 19 == 0) { dx = INT32_MIN; dy = INT32_MAX; }
        if (i % 23 == 0) { dx = dy = 0; }
        now += next() % 5;
        struct bbp_gamepad_report_body before = bbp_gamepad_snapshot(&s);
        bool changed = bbp_gamepad_motion(&s, dx, dy, now);
        struct bbp_gamepad_report_body after = bbp_gamepad_snapshot(&s);
        assert(changed == (memcmp(&before, &after, 9) != 0));
        assert(after.x >= -127 && after.y >= -127 && after.z >= -127 && after.rz >= -127);
        assert(right ? (!after.x && !after.y) : (!after.z && !after.rz));
        assert(!buttons_of(after) && after.hat == 8 && !after.lt && !after.rt);
        assert(bbp_gamepad_motion_remaining_ms(&s, now) <= 120);
        if (i % 1000 == 0) {
            now += 120;
            bool moving = after.x || after.y || after.z || after.rz;
            assert(bbp_gamepad_expire(&s, now) == moving); axes(&s, 0, 0, 0, 0);
            assert(bbp_gamepad_motion_remaining_ms(&s, now) == 0);
        }
    }
}
int main(int argc, char **argv) {
    /* Preserve runner's exact binary descriptor-dump CLI: no status text. */
    if (argc == 2 && !strcmp(argv[1], "--descriptor")) {
        return fwrite(descriptor, 1, sizeof(descriptor), stdout) == sizeof(descriptor) ? 0 : 1;
    }
    test_descriptor(); test_neutral(); test_all_button_dpad_states();
    test_owners_and_triggers(); test_modeled_edges(); test_click_mode_and_cancel(); test_keyboard_l3r3_and_guide_owners();
    test_stick_math_and_noise(); test_motion_timing_direction_and_limits();
    test_invalid_and_extreme_time(); test_motion_stress();
    puts("PASS gamepad core: 8192x16 button/Dpad states, 13 real usages, digital+analog triggers, 250000 owner edges, legacy click capture tests + D/K L3/R3 and P4/Space Guide dual owners, four virtual axes, symmetric velocity/deadzone/120ms expiry/clock bounds, 32768 motion stress cases; ID4, 9byte body/10byte USB, 72 descriptor bits; host only, not hardware");
    return 0;
}
