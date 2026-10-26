/* SPDX-License-Identifier: MIT
 * Additive oracle: the frozen legacy HOG test remains byte-identical.
 * The descriptor is a connection contract, not the current output route.
 * No radio, host parser, iPadOS or Android acceptance is asserted here.
 */
#define main legacy_gamepad_ble_main
#include "test_gamepad_ble.c"
#undef main

int main(void) {
    for (size_t i = 0; i < ARRAY_SIZE(peers); ++i) { peers[i].refs = 1; }
    reset_case();
    uint8_t expected[sizeof(zmk_hid_report_desc) + sizeof(bbp_gamepad_ble_descriptor)];
    uint8_t out[sizeof(expected)];
    memcpy(expected, zmk_hid_report_desc, sizeof(zmk_hid_report_desc));
    memcpy(expected + sizeof(zmk_hid_report_desc), bbp_gamepad_ble_descriptor,
           sizeof(bbp_gamepad_ble_descriptor));
    for (size_t peer = 0; peer < ARRAY_SIZE(peers); ++peer) {
        for (size_t offset = 0; offset < sizeof(expected);) {
            /* Mismatch requesting peer and active slot; change slot mid-read. */
            active_profile = (offset / 22U) % 2U ? 0 : 3;
            selected.ble.profile_index = (uint8_t)active_profile;
            active_connection = &peers[(peer + 1U) % ARRAY_SIZE(peers)];
            size_t count = sizeof(expected) - offset;
            if (count > 22U) { count = 22U; }
            ssize_t got = hog_svc.attrs[4].read(&peers[peer], &hog_svc.attrs[4],
                                               out + offset, (uint16_t)count,
                                               (uint16_t)offset);
            assert(got == (ssize_t)count);
            assert(memcmp(out + offset, expected + offset, count) == 0);
            offset += count;
        }
        assert(memcmp(out, expected, sizeof(expected)) == 0);
    }
    puts("PASS Map connection contract: 3 peers, route changes between Read Blob fragments; host only");
    return 0;
}
