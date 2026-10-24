/* SPDX-License-Identifier: MIT -- real pinned HID/HOG, host model only. */
#include <zephyr/bluetooth/gatt.h>
/* Equality semantics for the existing mock's 16-bit-only UUID representation. */
static inline int bt_uuid_cmp(const struct bt_uuid *a, const struct bt_uuid *b) {
    return (int)a->val - (int)b->val;
}
#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#endif
#ifndef CONFIG_BBP9981_BLE_HID_METADATA_COMPAT
#define CONFIG_BBP9981_BLE_HID_METADATA_COMPAT 0
#endif
#define main legacy_main
#include "test_gamepad_ble.c"
#undef main

_Static_assert(sizeof(info) == 4U, "Pinned HID Information ABI");
_Static_assert(sizeof(bbp_gamepad_ble_descriptor) == 141U, "Unchanged gamepad map");
#if CONFIG_ZMK_HID_REPORT_TYPE_HKRO && !CONFIG_ZMK_POINTING_SMOOTH_SCROLLING
_Static_assert(sizeof(zmk_hid_report_desc) == 196U, "Actual target stock map");
_Static_assert(sizeof(struct zmk_hid_keyboard_report_body) == 8U, "Actual HKRO6 body");
#endif
_Static_assert(sizeof(struct zmk_hid_consumer_report_body) == 12U, "Actual FULL6 body");
_Static_assert(sizeof(struct zmk_hid_mouse_report_body) == 9U, "Actual pointing body");

static void check_slice(const struct bt_gatt_attr *attr, const uint8_t *expected,
                        size_t total, uint16_t len, uint16_t offset) {
    uint8_t out[514];
    memset(out, 0xa5, sizeof(out));
    ssize_t got = attr->read(&peers[0], attr, out + 1, len, offset);
    size_t count = 0;
    if (offset > total) {
        assert(got == BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET));
    } else {
        count = total - offset;
        if (count > len) { count = len; }
        assert(got == (ssize_t)count && !memcmp(out + 1, expected + offset, count));
    }
    for (size_t i = 0; i < sizeof(out); ++i) {
        if (i == 0 || i > count) { assert(out[i] == 0xa5U); }
    }
}
static void test_hid_information(bool require_corrected) {
    reset_case();
    uint8_t original[4], expected[4], out[8];
    memcpy(original, &info, sizeof(original));
    memcpy(expected, original, sizeof(expected));
    if (CONFIG_BBP9981_BLE_HID_METADATA_COMPAT) { expected[0] = 0x11; expected[1] = 0x01; }
    assert(hog_svc.attrs[2].read(NULL, &hog_svc.attrs[2], out, sizeof(out), 0) == 4);
    if (require_corrected) {
        assert((out[0] == 0x11 && out[1] == 0x01) && "required corrected HID Information");
    }
    for (uint16_t offset = 0; offset <= 5; ++offset) {
        for (uint16_t len = 0; len <= 8; ++len) {
            check_slice(&hog_svc.attrs[2], expected, 4, len, offset);
        }
    }
    assert(!memcmp(&info, original, sizeof(original))); /* No producer mutation. */
    uint8_t raw[5] = {0x34, 0x12, 0xa7, 0x5a, 0x99};
    uint8_t raw_before[5];
    memcpy(raw_before, raw, sizeof(raw));
    assert(bbp_gamepad_ble_attr_read(NULL, &hog_svc.attrs[2], out, 8, 0, raw, 4) == 4);
    assert(out[0] == (CONFIG_BBP9981_BLE_HID_METADATA_COMPAT ? 0x11 : 0x34));
    assert(out[1] == (CONFIG_BBP9981_BLE_HID_METADATA_COMPAT ? 0x01 : 0x12));
    assert(out[2] == 0xa7 && out[3] == 0x5a && !memcmp(raw, raw_before, sizeof(raw)));
    for (uint16_t size = 3; size <= 5; size += 2) {
        assert(bbp_gamepad_ble_attr_read(NULL, &hog_svc.attrs[2], out, 8, 0, raw, size) == size);
        assert(!memcmp(out, raw, size)); /* Wrong-length Info must pass through. */
    }
    assert(bbp_gamepad_ble_attr_read(NULL, &hog_svc.attrs[2], out, 0, 0, NULL, 4) == 0);
    assert(bbp_gamepad_ble_attr_read(NULL, &hog_svc.attrs[6], out, 8, 0, raw, 4) == 4);
    assert(!memcmp(out, raw, 4)); /* Same length, different UUID: untouched. */
}
static void test_full_map_and_other_reads(void) {
    reset_case();
    uint8_t expected[sizeof(zmk_hid_report_desc) + sizeof(bbp_gamepad_ble_descriptor)], out[512];
    memcpy(expected, zmk_hid_report_desc, sizeof(zmk_hid_report_desc));
    memcpy(expected + sizeof(zmk_hid_report_desc), bbp_gamepad_ble_descriptor,
           sizeof(bbp_gamepad_ble_descriptor));
    /* ATT Read/Read Blob response payloads are MTU minus one byte. */
    const uint16_t lengths[] = {0, 1, 2, 7, 23 - 1, 65 - 1, 185 - 1, 247 - 1};
    for (uint16_t offset = 0; offset <= sizeof(expected) + 1U; ++offset) {
        for (size_t i = 0; i < ARRAY_SIZE(lengths); ++i) {
            check_slice(&hog_svc.attrs[4], expected, sizeof(expected), lengths[i], offset);
        }
    }
    uint8_t copied[sizeof(zmk_hid_report_desc)];
    memcpy(copied, zmk_hid_report_desc, sizeof(copied));
    assert(bbp_gamepad_ble_attr_read(NULL, &hog_svc.attrs[4], out, 512, 0, copied,
                                    sizeof(copied)) == sizeof(copied));
    assert(!memcmp(out, copied, sizeof(copied))); /* Equal bytes are not map identity. */
    assert(bbp_gamepad_ble_attr_read(NULL, &hog_svc.attrs[4], out, 512, 0,
                                    zmk_hid_report_desc, sizeof(copied) - 1U) == sizeof(copied) - 1U);
    assert(!memcmp(out, copied, sizeof(copied) - 1U));
    const void *bodies[] = {&keyboard.body, &consumer.body, &mouse.body};
    const size_t sizes[] = {sizeof(keyboard.body), sizeof(consumer.body), sizeof(mouse.body)};
    const size_t indexes[] = {6, 10, 14};
    for (size_t i = 0; i < ARRAY_SIZE(indexes); ++i) {
        const struct bt_gatt_attr *attr = &hog_svc.attrs[indexes[i]];
        for (uint16_t offset = 0; offset <= sizes[i] + 1U; ++offset) {
            check_slice(attr, bodies[i], sizes[i], 64, offset);
        }
    }
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--descriptor")) {
        uint8_t map[sizeof(zmk_hid_report_desc) + sizeof(bbp_gamepad_ble_descriptor)];
        assert(hog_svc.attrs[4].read(NULL, &hog_svc.attrs[4], map, sizeof(map), 0) == sizeof(map));
        return fwrite(map, 1, sizeof(map), stdout) == sizeof(map) ? 0 : 1;
    }
    bool require_corrected = argc == 2 && !strcmp(argv[1], "--require-corrected-info");
    assert(argc == 1 || require_corrected);
    for (size_t i = 0; i < ARRAY_SIZE(peers); ++i) { peers[i].refs = 1; }
    test_hid_information(require_corrected);
    test_full_map_and_other_reads();
    /* Frozen individual oracles; only the old raw-Info combined test is replaced. */
    RUN(test_pinned_service_old_attrs_and_tail);
    RUN(test_tail_contract_fails_closed);
    RUN(test_report_map_all_offsets_crossing_and_end);
    RUN(test_read_nine_byte_body_and_offsets);
    RUN(test_read_wrong_peer_route_and_reconnected_object);
    RUN(test_read_epoch_and_peer_changes_during_getter);
    RUN(test_ccc_is_independent_and_per_peer);
    RUN(test_send_value_body_and_caller_reference);
    RUN(test_send_refuses_unsubscribed_or_not_connected);
    RUN(test_trigger_only_requires_current_context_and_peer);
    RUN(test_send_rechecks_epoch_and_active_before_submission);
    RUN(test_neutral_releases_only_old_captured_peer);
    RUN(test_zero_filled_is_not_neutral_and_errors_are_passthrough);
    RUN(test_original_stock_senders_remain);
#if BBP_GAMEPAD_TEST_WITH_SNAPSHOT
    RUN(test_existing_mouse_snapshot_uses_original_index13);
#endif
    balanced();
    printf("PASS real HID metadata compat=%d NKRO=%d smooth=%d snapshot=%d; HOST MODEL ONLY\n",
           CONFIG_BBP9981_BLE_HID_METADATA_COMPAT, CONFIG_ZMK_HID_REPORT_TYPE_NKRO,
           CONFIG_ZMK_POINTING_SMOOTH_SCROLLING, BBP_GAMEPAD_TEST_WITH_SNAPSHOT);
    return 0;
}
