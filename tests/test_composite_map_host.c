/* SPDX-License-Identifier: MIT
 * Tests only the isolated composite map/codec model, not a real HOG service.
 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "host oracle requires enabled assertions"
#endif
#include "../experiments/gamepad_protocol/composite_map_host.h"

static uint8_t stock[BBP_COMPOSITE_STOCK_MAP_SIZE];

static void reference_map(uint8_t *out) {
    memcpy(out, stock, sizeof(stock));
    memcpy(out + sizeof(stock), bbp_exp_report_map, 8U);
    out[sizeof(stock) + 7U] = BBP_COMPOSITE_INPUT_ID;
    out[sizeof(stock) + 8U] = 0x15;
    out[sizeof(stock) + 9U] = 0x00;
    memcpy(out + sizeof(stock) + 10U, bbp_exp_report_map + 8U,
           sizeof(bbp_exp_report_map) - 8U);
}

static void test_map_reads(void) {
    uint8_t expected[BBP_COMPOSITE_MAP_SIZE];
    reference_map(expected);
    static const size_t lens[] = {0, 1, 2, 7, 22, 64, BBP_COMPOSITE_MAP_SIZE, 512};
    for (size_t off = 0; off <= BBP_COMPOSITE_MAP_SIZE + 1U; ++off) {
        for (size_t j = 0; j < sizeof(lens) / sizeof(lens[0]); ++j) {
            uint8_t buffer[512];
            memset(buffer, 0xa5, sizeof(buffer));
            int got = bbp_composite_map_read(stock, sizeof(stock), off, buffer, lens[j]);
            if (off > BBP_COMPOSITE_MAP_SIZE) {
                assert(got == -ERANGE);
                for (size_t k = 0; k < sizeof(buffer); ++k) { assert(buffer[k] == 0xa5); }
                continue;
            }
            size_t want = BBP_COMPOSITE_MAP_SIZE - off;
            if (want > lens[j]) { want = lens[j]; }
            assert(got == (int)want);
            assert(memcmp(buffer, expected + off, want) == 0);
            for (size_t k = want; k < sizeof(buffer); ++k) { assert(buffer[k] == 0xa5); }
        }
    }
    assert(bbp_composite_map_read(stock, sizeof(stock), SIZE_MAX, NULL, 0) == -ERANGE);
    assert(bbp_composite_map_read(stock, sizeof(stock), BBP_COMPOSITE_MAP_SIZE, NULL, 0) == 0);
    assert(bbp_composite_map_read(NULL, sizeof(stock), 0, NULL, 0) == -EINVAL);
    assert(bbp_composite_map_read(stock, sizeof(stock)-1U, 0, NULL, 0) == -EINVAL);
    assert(bbp_composite_map_read(stock, sizeof(stock), 0, NULL, 1) == -EINVAL);
    uint8_t alias[sizeof(stock) + 16U], saved[sizeof(alias)];
    memset(alias, 0x4c, sizeof(alias));
    memcpy(alias + 8U, stock, sizeof(stock));
    memcpy(saved, alias, sizeof(alias));
    assert(bbp_composite_map_read(alias + 8U, sizeof(stock), 0, alias + 9U, 10U) == -EINVAL);
    assert(!memcmp(alias, saved, sizeof(alias)));
    assert(bbp_composite_map_read(alias + 8U, sizeof(stock), 0, alias + 4U, 10U) == -EINVAL);
    assert(!memcmp(alias, saved, sizeof(alias)));
    assert(bbp_composite_map_read(stock, sizeof(stock), 0,
                                  (void *)(uintptr_t)bbp_exp_report_map, 1U) == -EINVAL);

    /* Interleaved callers simulate different read chunk sizes; this function
     * has no connection or profile state. It does not simulate a real ATT stack. */
    uint8_t joined[3][BBP_COMPOSITE_MAP_SIZE] = {{0}};
    size_t pos[3] = {0}, chunks[3] = {20, 22, 37};
    bool pending = true;
    while (pending) {
        pending = false;
        for (unsigned int peer = 0; peer < 3; ++peer) {
            if (pos[peer] == BBP_COMPOSITE_MAP_SIZE) { continue; }
            pending = true;
            int got = bbp_composite_map_read(stock, sizeof(stock), pos[peer],
                                              joined[peer] + pos[peer], chunks[peer]);
            assert(got > 0);
            pos[peer] += (size_t)got;
        }
    }
    for (unsigned int peer = 0; peer < 3; ++peer) {
        assert(memcmp(joined[peer], expected, sizeof(expected)) == 0);
    }
    assert(bbp_composite_input_ref[0] == 4U && bbp_composite_input_ref[1] == 1U);
    assert(bbp_composite_output_ref[0] == 5U && bbp_composite_output_ref[1] == 2U);
}

static void test_codec(void) {
    static const uint8_t neutral[10] = {15,0,0,128,128,128,128,0,0,0};
    struct { uint8_t before, data[10], after; } out = {.before=0x6a,.after=0xb7};
    struct bbp_gamepad_report_body input = bbp_gamepad_neutral();
    struct bbp_gamepad_report_body original = input;
    assert(bbp_composite_encode(&input, out.data, sizeof(out.data)) == 0);
    assert(memcmp(out.data, neutral, sizeof(neutral)) == 0);
    assert(!memcmp(&input, &original, sizeof(input)) && out.before == 0x6a && out.after == 0xb7);
    for (int axis = -127; axis <= 127; ++axis) {
        input = bbp_gamepad_neutral();
        input.x = (int8_t)axis;
        assert(bbp_composite_encode(&input, out.data, sizeof(out.data)) == 0);
        assert(out.data[3] == (uint8_t)(axis + 128));
        assert(out.before == 0x6a && out.after == 0xb7);
    }
    input = bbp_gamepad_neutral(); input.buttons_lo = 1U; input.x = 127; input.lt = 255U;
    static const uint8_t expected[10] = {15,4,64,255,128,128,128,255,0,0};
    original = input;
    assert(bbp_composite_encode(&input, out.data, sizeof(out.data)) == 0);
    assert(!memcmp(out.data, expected, sizeof(expected)) && !memcmp(&input, &original, sizeof(input)));
    input = bbp_gamepad_neutral(); input.lt = 64U;
    assert(bbp_composite_encode(&input, out.data, sizeof(out.data)) == 0 && !(out.data[1] & 4U));
    input.lt = 65U;
    assert(bbp_composite_encode(&input, out.data, sizeof(out.data)) == 0 && (out.data[1] & 4U));
    input = bbp_gamepad_neutral(); input.hat = 9U;
    assert(bbp_composite_encode(&input, out.data, sizeof(out.data)) == -EINVAL);
    assert(!memcmp(out.data, neutral, sizeof(neutral)));
    input = bbp_gamepad_neutral(); input.x = INT8_MIN;
    assert(bbp_composite_encode(&input, out.data, sizeof(out.data)) == -EINVAL);
    assert(!memcmp(out.data, neutral, sizeof(neutral)));
    memset(out.data, 0xa5, sizeof(out.data));
    assert(bbp_composite_encode(&input, out.data, 9U) == -EINVAL);
    for (unsigned int i = 0; i < sizeof(out.data); ++i) { assert(out.data[i] == 0xa5); }
    assert(bbp_composite_encode(&input, NULL, 10U) == -EINVAL);
    assert(bbp_composite_encode(NULL, out.data, sizeof(out.data)) == -EINVAL);
    assert(!memcmp(out.data, neutral, sizeof(neutral)));

    struct { uint8_t prefix[10]; struct bbp_gamepad_report_body input; uint8_t suffix[12]; } storage;
    memset(&storage, 0x4c, sizeof(storage));
    storage.input = bbp_gamepad_neutral();
    uint8_t saved[sizeof(storage)]; memcpy(saved, &storage, sizeof(saved));
    uint8_t *bytes = (uint8_t *)(void *)&storage;
    size_t input_offset = (size_t)((uint8_t *)(void *)&storage.input - bytes);
    assert(bbp_composite_encode(&storage.input, bytes + input_offset + 1U, 10U) == -EINVAL);
    assert(!memcmp(&storage, saved, sizeof(saved)));
    assert(bbp_composite_encode(&storage.input, bytes + input_offset - 5U, 10U) == -EINVAL);
    assert(!memcmp(&storage, saved, sizeof(saved)));
    assert(out.before == 0x6a && out.after == 0xb7);
}

int main(int argc, char **argv) {
    if (fread(stock, 1U, sizeof(stock), stdin) != sizeof(stock) || getchar() != EOF) { return 2; }
    if (argc == 2 && !strcmp(argv[1], "--map")) {
        uint8_t map[BBP_COMPOSITE_MAP_SIZE];
        if (bbp_composite_map_read(stock, sizeof(stock), 0, map, sizeof(map)) != (int)sizeof(map)) { return 3; }
        return fwrite(map, 1U, sizeof(map), stdout) == sizeof(map) ? 0 : 4;
    }
    if (argc == 2 && !strcmp(argv[1], "--pure")) {
        return fwrite(bbp_exp_report_map, 1U, sizeof(bbp_exp_report_map), stdout) == sizeof(bbp_exp_report_map) ? 0 : 7;
    }
    if (argc == 2 && !strcmp(argv[1], "--sample")) {
        struct bbp_gamepad_report_body input = bbp_gamepad_neutral();
        input.buttons_lo = 1U; input.x = 127; input.lt = 255U;
        uint8_t body[BBP_EXP_INPUT_SIZE];
        if (bbp_composite_encode(&input, body, sizeof(body)) != 0) { return 5; }
        return fwrite(body, 1U, sizeof(body), stdout) == sizeof(body) ? 0 : 6;
    }
    if (argc == 2 && !strcmp(argv[1], "--refs")) {
        printf("%u %u\n%u %u\n", bbp_composite_input_ref[0], bbp_composite_input_ref[1],
               bbp_composite_output_ref[0], bbp_composite_output_ref[1]);
        return 0;
    }
    test_map_reads(); test_codec();
    puts("PASS HOST-ONLY composite map slice/codec model: exact stock prefix, all offsets, 3 interleaved callers, neutral/axes/canaries/overlap; NOT real GATT, radio or OS evidence");
    return 0;
}
