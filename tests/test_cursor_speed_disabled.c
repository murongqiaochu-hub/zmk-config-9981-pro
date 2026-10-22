/* SPDX-License-Identifier: MIT
 * An always-present DTS behavior must have an implementation even when the
 * cursor-speed feature is OFF. This executable deliberately never includes or
 * links trackpad_cursor_speed.c and supplies NO cursor-service stub functions.
 * Any accidental service call therefore fails the real linker, not an oracle.
 *
 * Default: leave CONFIG_BBP9981_CURSOR_SPEED undefined, like an OFF Kconfig.
 * Also build with -DCONFIG_BBP9981_CURSOR_SPEED=0. Run both settings and metadata
 * values; settings=0 mocks deliberately expose no settings backend API at all.
 *
 * cc -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=undefined \
 *    -fno-sanitize-recover=all -Itests/cursor_speed_mocks \
 *    tests/test_cursor_speed_disabled.c -o /tmp/test_cursor_speed_disabled
 * /tmp/test_cursor_speed_disabled
 */
#if defined(CONFIG_BBP9981_CURSOR_SPEED) && CONFIG_BBP9981_CURSOR_SPEED != 0
#error "This test must compile the actual disabled cursor-speed behavior"
#endif
#include "cursor_speed_mocks/cursor_speed_mock_env.h"
#include "../config/boards/arm/bbp9981/custom_driver/behavior_cursor_speed.c"

static void assert_no_effects(void) {
    assert(mock_lock_depth == 0U);
    assert(mock_scheduled_work == NULL && mock_work_runs == 0U);
#if CONFIG_SETTINGS
    assert(mock_save_attempts == 0U && mock_save_successes == 0U);
    assert(!mock_flash_present && mock_save_hook == NULL);
#endif
}

static void edge(uint32_t position, bool down, int captured_layer) {
    struct zmk_behavior_binding binding = {
        .behavior_dev = "cursor_speed", .param1 = 0U, .param2 = 0U,
    };
    struct zmk_behavior_binding_event event = {
        .position = position, .layer = captured_layer, .timestamp = mock_now,
    };
    const struct behavior_driver_api *api = mock_cursor_behavior_api_0;
    assert(api != NULL && api->binding_pressed != NULL && api->binding_released != NULL);
    int ret = down ? api->binding_pressed(&binding, event) : api->binding_released(&binding, event);
    /* The dedicated mock uses17 rather than0, rejecting a literal-zero return. */
    assert(ret == ZMK_BEHAVIOR_OPAQUE);
    assert_no_effects();
    ++mock_now;
}

static void test_all_positions_duplicate_edges_and_layer_changes(void) {
    for (int layer = 0; layer < 4; ++layer) {
        for (uint32_t position = 0U; position < 42U; ++position) {
            edge(position, false, layer); /* Orphan UP. */
            edge(position, true, layer);
            for (unsigned repeat = 0U; repeat < 16U; ++repeat) {
                edge(position, true, layer);
            }
            mock_advance_to(mock_now + 1000); /* Held key cannot arm repeat/save work. */
            assert_no_effects();
            edge(position, true, (layer + 1) % 4);
            edge(position, false, (layer + 2) % 4); /* Captured position, changed layer. */
            edge(position, false, layer);
            edge(position, true, 2); /* A later UPPER press still has a real no-op API. */
            edge(position, false, 0);
        }
    }
}

static void test_simultaneous_positions_and_reverse_release(void) {
    for (uint32_t position = 0U; position < 42U; ++position) {
        edge(position, true, 2);
    }
    for (uint32_t position = 0U; position < 42U; ++position) {
        edge(position, true, 0);
        edge(position, true, 3);
    }
    mock_advance_to(mock_now + 1000000);
    assert_no_effects();
    for (uint32_t remaining = 42U; remaining > 0U; --remaining) {
        edge(remaining - 1U, false, 0);
        edge(remaining - 1U, false, 3);
    }
    for (uint32_t position = 0U; position < 42U; ++position) {
        edge(position, true, 2);
        edge(position, false, 1);
    }
}

static void test_invalid_positions_are_opaque_without_side_effects(void) {
    const uint32_t invalid[] = {42U, 43U, 63U, 64U, 255U, UINT32_MAX};
    for (unsigned i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        for (int layer = 0; layer < 4; ++layer) {
            edge(invalid[i], false, layer);
            edge(invalid[i], true, layer);
            edge(invalid[i], true, 2);
            edge(invalid[i], false, 0);
            edge(invalid[i], false, 3);
        }
    }
}

static void test_actual_metadata_registration(void) {
#if CONFIG_ZMK_BEHAVIOR_METADATA
    const struct behavior_driver_api *api = mock_cursor_behavior_api_0;
    assert(api->get_parameter_metadata == zmk_behavior_get_empty_param_metadata);
    struct device device = {0};
    struct behavior_parameter_metadata metadata = {0};
    assert(api->get_parameter_metadata(&device, &metadata) == 0);
#else
    assert(mock_cursor_behavior_api_0->get_parameter_metadata == NULL);
#endif
    assert_no_effects();
}

int main(void) {
    assert_no_effects();
    test_all_positions_duplicate_edges_and_layer_changes();
    test_simultaneous_positions_and_reverse_release();
    test_invalid_positions_are_opaque_without_side_effects();
    test_actual_metadata_registration();
    mock_advance_to(mock_now + 1000000);
    assert_no_effects();
    puts("PASS disabled cursor-speed actual behavior links WITHOUT service:42 positions, duplicate DOWN/UP, held/layer changes, bounds, metadata and no settings/work/backend effects");
    return 0;
}
