/* SPDX-License-Identifier: MIT
 * Compile the actual service AND behavior, not a test reimplementation.
 * Example (run both CONFIG_SETTINGS values):
 * cc -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=undefined \
 *    -fno-sanitize-recover=all -Itests/cursor_speed_mocks \
 *    -DCONFIG_SETTINGS=1 tests/test_cursor_speed.c -o /tmp/test_cursor_speed_on
 * /tmp/test_cursor_speed_on
 * Replace CONFIG_SETTINGS=1 with =0 and use another output for volatile mode.
 */
#include "cursor_speed_mocks/cursor_speed_mock_env.h"
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_cursor_speed.c"
#include "../config/boards/arm/bbp9981/custom_driver/behavior_cursor_speed.c"

static uint8_t current_index(void) {
    return bbp_cursor_speed_snapshot_index(bbp_cursor_speed_snapshot());
}

/* A simulated reboot resets the actual file-local service/behavior state and
 * work object. The durable mock record optionally survives. This is not an
 * alternate implementation of cycle, load, debounce, saving, or held handling.
 */
static void reset_boot(bool preserve_flash) {
    assert(mock_lock_depth == 0U);
    cursor_speed_lock = (struct k_spinlock){0};
    atomic_set(&cursor_speed_state, (atomic_val_t)BBP_CURSOR_SPEED_DEFAULT_INDEX);
    atomic_set(&cursor_speed_motion_state, (atomic_val_t)BBP_CURSOR_SPEED_DEFAULT_INDEX);
    cursor_speed_held_lock = (struct k_spinlock){0};
    cursor_speed_held = 0U;
    mock_now = 0;
    mock_scheduled_work = NULL;
    mock_work_runs = 0U;
#if CONFIG_SETTINGS
    cursor_load_committed = false;
    cursor_staged_valid = false;
    cursor_staged_index = 0U;
    cursor_user_changed = false;
    cursor_save_dirty = false;
    cursor_save_running = false;
    cursor_save_failures = 0U;
    cursor_save_after = 0;
    cursor_speed_save_work = (struct k_work_delayable){
        .work = {.handler = cursor_speed_save_worker},
    };
    mock_save_attempts = mock_save_successes = 0U;
    mock_save_error = 0;
    mock_save_hook = NULL;
    memset(mock_save_records, 0, sizeof(mock_save_records));
    memset(mock_save_times, 0, sizeof(mock_save_times));
    if (!preserve_flash) {
        mock_flash_present = false;
        memset(mock_flash_record, 0, sizeof(mock_flash_record));
    }
#else
    (void)preserve_flash;
#endif
}

static void test_motion_frame_linearization(void) {
    reset_boot(false);
    uint32_t before = bbp_cursor_speed_begin_motion_frame();
    assert(before == 2U && bbp_cursor_speed_motion_gain(false) == 480U);
    assert(bbp_cursor_speed_cycle() == 0);
    /* A mid-I2C/output selection cannot split the RAW shadow/cursor epoch. */
    assert(bbp_cursor_speed_gain(false) == 600U);
    assert(bbp_cursor_speed_motion_gain(false) == 480U);
    assert(bbp_cursor_speed_motion_gain(true) == 240U);
    uint32_t next = bbp_cursor_speed_begin_motion_frame();
    assert(next != before && next == bbp_cursor_speed_snapshot());
    assert(bbp_cursor_speed_motion_gain(false) == 600U);
    for (unsigned n=0; n<5U; ++n) { assert(bbp_cursor_speed_cycle() == 0); }
    assert(bbp_cursor_speed_motion_gain(false) == 600U);
    uint32_t roundtrip = bbp_cursor_speed_begin_motion_frame();
    assert(roundtrip != next && bbp_cursor_speed_snapshot_index(roundtrip) == 3U);
    assert(bbp_cursor_speed_motion_gain(true) == 300U);
    assert(mock_scheduled_work == NULL);
}

static void test_default_table_half_and_generation(void) {
    static const uint16_t expected_gain[5] = {240U, 360U, 480U, 600U, 720U};
    static const uint8_t expected_cycle[5] = {3U, 4U, 0U, 1U, 2U};
    reset_boot(false);
    uint32_t initial = bbp_cursor_speed_snapshot();
    assert(initial == 2U && current_index() == 2U);
    assert(bbp_cursor_speed_gain(false) == 480U && bbp_cursor_speed_gain(true) == 240U);
    assert(BBP_CURSOR_SPEED_DENOMINATOR == 400U);
    for (uint8_t index = 0U; index < 5U; ++index) {
        assert(bbp_cursor_speed_table_gain(index, false) == expected_gain[index]);
        assert(bbp_cursor_speed_table_gain(index, true) == expected_gain[index] / 2U);
    }
    assert(bbp_cursor_speed_table_gain(5U, false) == 480U);
    assert(bbp_cursor_speed_table_gain(UINT8_MAX, true) == 240U);
    uint32_t previous = initial;
    for (unsigned step = 0U; step < 5U; ++step) {
        assert(bbp_cursor_speed_cycle() == 0);
        uint32_t snapshot = bbp_cursor_speed_snapshot();
        assert(snapshot != previous);
        assert(current_index() == expected_cycle[step]);
        assert(bbp_cursor_speed_snapshot_generation(snapshot) == step + 1U);
        assert(bbp_cursor_speed_gain(false) == expected_gain[expected_cycle[step]]);
        assert(bbp_cursor_speed_gain(true) == expected_gain[expected_cycle[step]] / 2U);
        previous = snapshot;
    }
    assert(current_index() == 2U && previous != initial);
    assert(mock_scheduled_work == NULL); /* Before commit: no premature backend work. */
#if CONFIG_SETTINGS
    assert(mock_save_attempts == 0U && cursor_save_dirty);
#endif

    /* Defined unsigned wrap of the finite-width token, not every five presses. */
    atomic_set(&cursor_speed_state, (atomic_val_t)UINT32_C(0xfffffffa));
    assert(bbp_cursor_speed_cycle() == 0);
    assert(bbp_cursor_speed_snapshot() == 3U);
}

#if CONFIG_SETTINGS
static const struct mock_settings_handler *const settings_handler =
    &mock_handler_bbp9981_cursor_speed;
struct record_reader {
    uint8_t bytes[3];
    ssize_t result;
    unsigned calls;
    void (*hook)(void);
};
static struct record_reader reader_for(uint8_t version, uint8_t index) {
    return (struct record_reader){.bytes = {version, index, 0U}, .result = 2};
}
static ssize_t read_record(void *arg, void *data, size_t len) {
    struct record_reader *reader = arg;
    assert(mock_lock_depth == 0U && len == 2U);
    ++reader->calls;
    if (reader->result > 0) {
        size_t copy = (size_t)reader->result;
        if (copy > len) { copy = len; }
        memcpy(data, reader->bytes, copy);
    }
    if (reader->hook != NULL) {
        void (*hook)(void) = reader->hook;
        reader->hook = NULL;
        hook();
    }
    return reader->result;
}
static void stage_index(uint8_t index) {
    struct record_reader reader = reader_for(1U, index);
    assert(settings_handler->set("gain_v1", 2U, read_record, &reader) == 0);
    assert(reader.calls == 1U);
}
static void commit_settings(void) {
    assert(settings_handler->commit() == 0);
    assert(mock_lock_depth == 0U);
}
static void load_durable(void) {
    if (mock_flash_present) {
        struct record_reader reader = reader_for(mock_flash_record[0], mock_flash_record[1]);
        assert(settings_handler->set("gain_v1", 2U, read_record, &reader) == 0);
    }
    commit_settings();
}

static void test_debounce_merge_and_premature_wakeup(void) {
    assert(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE == 1000);
    reset_boot(false);
    commit_settings();
    assert(mock_scheduled_work == NULL && mock_save_attempts == 0U);
    assert(bbp_cursor_speed_cycle() == 0); /* 1.5 immediately, first deadline 1000. */
    assert(bbp_cursor_speed_gain(false) == 600U && cursor_speed_save_work.due == 1000);
    mock_advance_to(999);
    assert(mock_save_attempts == 0U);
    assert(bbp_cursor_speed_cycle() == 0); /* 1.8, deadline now 1999. */
    mock_advance_to(1500);
    assert(mock_save_attempts == 0U);
    assert(bbp_cursor_speed_cycle() == 0); /* 0.6, deadline now 2500. */
    assert(current_index() == 0U && cursor_speed_save_work.due == 2500);
    /* A timer already dispatched before a concurrent cycle must recheck time. */
    cursor_speed_save_worker(&cursor_speed_save_work.work);
    assert(mock_save_attempts == 0U && cursor_speed_save_work.due == 2500);
    mock_advance_to(2499);
    assert(mock_save_attempts == 0U);
    mock_advance_to(2500);
    assert(mock_save_attempts == 1U && mock_save_successes == 1U);
    assert(mock_save_times[0] == 2500 && mock_save_records[0][0] == 1U);
    assert(mock_flash_record[1] == 0U && !cursor_save_dirty);
    assert(!cursor_speed_save_work.pending);
    mock_advance_to(100000);
    assert(mock_save_attempts == 1U);
}

static void test_fullcycle_same_saved_index_and_reboot(void) {
    reset_boot(false);
    stage_index(2U);
    commit_settings();
    uint32_t initial = bbp_cursor_speed_snapshot();
    for (unsigned n = 0U; n < 5U; ++n) {
        assert(bbp_cursor_speed_cycle() == 0);
    }
    assert(current_index() == 2U && bbp_cursor_speed_snapshot() != initial);
    assert(cursor_save_dirty && mock_save_attempts == 0U);
    mock_advance_to(999);
    assert(mock_save_attempts == 0U);
    mock_advance_to(1000);
    assert(mock_save_attempts == 1U && mock_flash_record[1] == 2U);
    assert(!cursor_save_dirty);
    reset_boot(true);
    load_durable();
    assert(current_index() == 2U && bbp_cursor_speed_snapshot_generation(
        bbp_cursor_speed_snapshot()) == 0U);
    mock_advance_to(10000);
    assert(mock_save_attempts == 0U); /* Restoring the saved default is not a migration. */

    /* Save a nondefault final choice, then restore it in a fresh RAM boot. */
    assert(bbp_cursor_speed_cycle() == 0);
    assert(bbp_cursor_speed_cycle() == 0);
    assert(current_index() == 4U);
    mock_advance_to(11000);
    assert(mock_flash_record[0] == 1U && mock_flash_record[1] == 4U);
    reset_boot(true);
    assert(current_index() == 2U);
    load_durable();
    assert(current_index() == 4U && bbp_cursor_speed_gain(false) == 720U);
    assert(bbp_cursor_speed_snapshot_generation(bbp_cursor_speed_snapshot()) == 1U);
    assert(!cursor_save_dirty && mock_scheduled_work == NULL);
    mock_advance_to(100000);
    assert(mock_save_attempts == 0U);
}

static void test_every_loaded_index_and_one_shot_commit(void) {
    assert(strcmp(settings_handler->subtree, "bbp9981_cursor") == 0);
    for (uint8_t index = 0U; index < 5U; ++index) {
        reset_boot(false);
        stage_index(index);
        assert(current_index() == 2U); /* h_set only stages. */
        commit_settings();
        assert(current_index() == index);
        uint32_t restored = bbp_cursor_speed_snapshot();
        assert(bbp_cursor_speed_snapshot_generation(restored) == (index == 2U ? 0U : 1U));
        for (unsigned n = 0U; n < 4U; ++n) { commit_settings(); }
        stage_index((uint8_t)((index + 1U) % 5U)); /* A repeated load is not live restore. */
        commit_settings();
        assert(bbp_cursor_speed_snapshot() == restored && !cursor_save_dirty);
        mock_advance_to(100000);
        assert(mock_save_attempts == 0U && mock_scheduled_work == NULL);
    }
    /* An early empty commit also consumes the boot restore barrier. No later
     * service init exists that can reset/replay it or strand initialized work.
     */
    reset_boot(false);
    commit_settings();
    stage_index(0U);
    commit_settings();
    assert(current_index() == 2U && mock_scheduled_work == NULL);
}

static void cycle_in_read(void) {
    assert(mock_lock_depth == 0U);
    assert(bbp_cursor_speed_cycle() == 0);
}
static void test_user_precedence_early_late_and_reentrant_read(void) {
    for (unsigned order = 0U; order < 3U; ++order) {
        reset_boot(false);
        mock_advance_to(100);
        if (order == 0U) { stage_index(0U); }
        if (order == 2U) {
            struct record_reader reader = reader_for(1U, 0U);
            reader.hook = cycle_in_read; /* cycle during unlocked settings read. */
            assert(settings_handler->set("gain_v1", 2U, read_record, &reader) == 0);
        } else {
            assert(bbp_cursor_speed_cycle() == 0);
            if (order == 1U) { stage_index(0U); }
        }
        uint32_t chosen = bbp_cursor_speed_snapshot();
        assert(current_index() == 3U && bbp_cursor_speed_snapshot_generation(chosen) == 1U);
        assert(cursor_save_after == 1100 && mock_scheduled_work == NULL);
        mock_advance_to(10000); /* Late initial commit cannot overwrite or lose RAM intent. */
        assert(mock_save_attempts == 0U);
        commit_settings();
        assert(bbp_cursor_speed_snapshot() == chosen && cursor_speed_save_work.due == 10000);
        commit_settings();
        stage_index(1U);
        commit_settings();
        assert(bbp_cursor_speed_snapshot() == chosen);
        mock_advance_to(10000); /* Original debounce elapsed: save immediately at barrier. */
        assert(mock_save_attempts == 1U && mock_flash_record[1] == 3U);
        stage_index(4U);
        commit_settings();
        mock_advance_to(100000);
        assert(bbp_cursor_speed_snapshot() == chosen && mock_save_attempts == 1U);
    }
    /* Initial commit before the original deadline retains the remaining delay. */
    reset_boot(false);
    assert(bbp_cursor_speed_cycle() == 0);
    mock_advance_to(400);
    stage_index(0U);
    commit_settings();
    assert(cursor_speed_save_work.due == 1000 && current_index() == 3U);
    mock_advance_to(999);
    assert(mock_save_attempts == 0U);
    mock_advance_to(1000);
    assert(mock_save_attempts == 1U && mock_flash_record[1] == 3U);
}

static void test_invalid_unknown_short_and_negative_records(void) {
    reset_boot(false);
    struct record_reader reader = reader_for(1U, 0U);
    const char *unknown[] = {"gain", "gain_v1/child", "gain_v2", "", NULL};
    for (unsigned i = 0U; i < sizeof(unknown) / sizeof(unknown[0]); ++i) {
        assert(settings_handler->set(unknown[i], 2U, read_record, &reader) == -ENOENT);
    }
    const size_t lengths[] = {0U, 1U, 3U, 255U};
    for (unsigned i = 0U; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        assert(settings_handler->set("gain_v1", lengths[i], read_record, &reader) == -EINVAL);
    }
    assert(reader.calls == 0U);
    assert(settings_handler->set("gain_v1", 2U, NULL, &reader) == -EINVAL);
    const uint8_t versions[] = {0U, 2U, UINT8_MAX};
    for (unsigned i = 0U; i < sizeof(versions) / sizeof(versions[0]); ++i) {
        reader = reader_for(versions[i], 0U);
        assert(settings_handler->set("gain_v1", 2U, read_record, &reader) == -EINVAL);
    }
    const uint8_t indices[] = {5U, UINT8_MAX};
    for (unsigned i = 0U; i < sizeof(indices) / sizeof(indices[0]); ++i) {
        reader = reader_for(1U, indices[i]);
        assert(settings_handler->set("gain_v1", 2U, read_record, &reader) == -EINVAL);
    }
    const ssize_t lengths_read[] = {0, 1, 3, -EIO, -ENOMEM};
    for (unsigned i = 0U; i < sizeof(lengths_read) / sizeof(lengths_read[0]); ++i) {
        reader = reader_for(1U, 0U);
        reader.result = lengths_read[i];
        int expected = lengths_read[i] < 0 ? (int)lengths_read[i] : -EINVAL;
        assert(settings_handler->set("gain_v1", 2U, read_record, &reader) == expected);
    }
    assert(current_index() == 2U && !cursor_staged_valid && !cursor_save_dirty);
    commit_settings();
    mock_advance_to(100000);
    assert(current_index() == 2U && mock_save_attempts == 0U);

    /* A rejected record must neither publish nor corrupt an earlier valid stage. */
    reset_boot(false);
    stage_index(4U);
    reader = reader_for(0U, 1U);
    assert(settings_handler->set("gain_v1", 2U, read_record, &reader) == -EINVAL);
    commit_settings();
    assert(current_index() == 4U && mock_save_attempts == 0U);
}

static void test_read_animation_and_movement_simulation_never_save(void) {
    reset_boot(false);
    stage_index(4U);
    commit_settings();
    uint32_t restored = bbp_cursor_speed_snapshot();
    volatile uint32_t simulated_output = 0U;
    for (unsigned frame = 0U; frame < 50000U; ++frame) {
        /* Simulate polling, RAW-shadow gain reads, cursor/filter prepare and
         * unrelated animation ticks. None is a settings mutation entrypoint.
         */
        simulated_output += bbp_cursor_speed_gain((frame & 1U) != 0U);
        simulated_output ^= bbp_cursor_speed_snapshot();
        mock_advance_to((int64_t)frame * 17);
    }
    (void)simulated_output;
    assert(bbp_cursor_speed_snapshot() == restored);
    assert(mock_save_attempts == 0U && mock_scheduled_work == NULL);
    assert(!cursor_save_dirty);

    assert(bbp_cursor_speed_cycle() == 0);
    int64_t deadline = mock_now + 1000;
    for (unsigned frame = 0U; frame < 999U; ++frame) {
        assert(bbp_cursor_speed_gain(false) == 240U);
        (void)bbp_cursor_speed_gain(true);
        (void)bbp_cursor_speed_snapshot();
        mock_advance_to(mock_now + 1);
    }
    assert(mock_save_attempts == 0U && cursor_speed_save_work.due == deadline);
    mock_advance_to(deadline);
    assert(mock_save_attempts == 1U && mock_flash_record[1] == 0U);
}

static void test_failure_budget_and_new_action_rearms(void) {
    reset_boot(false);
    commit_settings();
    mock_save_error = -EIO;
    assert(bbp_cursor_speed_cycle() == 0);
    mock_advance_to(999);
    assert(mock_save_attempts == 0U);
    for (unsigned attempt = 1U; attempt <= 3U; ++attempt) {
        mock_advance_to((int64_t)attempt * 1000);
        assert(mock_save_attempts == attempt && cursor_save_failures == attempt);
        assert(cursor_save_dirty && mock_save_successes == 0U);
    }
    assert(!cursor_speed_save_work.pending);
    mock_advance_to(100000);
    for (unsigned n = 0U; n < 10U; ++n) {
        commit_settings();
        (void)bbp_cursor_speed_gain(false);
        cursor_speed_save_worker(&cursor_speed_save_work.work);
    }
    assert(mock_save_attempts == 3U && !cursor_speed_save_work.pending);
    mock_save_error = 0;
    assert(bbp_cursor_speed_cycle() == 0);
    assert(cursor_save_failures == 0U && cursor_speed_save_work.due == 101000);
    mock_advance_to(100999);
    assert(mock_save_attempts == 3U);
    mock_advance_to(101000);
    assert(mock_save_attempts == 4U && mock_save_successes == 1U);
    assert(mock_flash_record[1] == 4U && !cursor_save_dirty);
    mock_advance_to(1000000);
    assert(mock_save_attempts == 4U);
}

static void test_new_action_during_retry_restarts_debounce(void) {
    reset_boot(false);
    commit_settings();
    mock_save_error = -EIO;
    assert(bbp_cursor_speed_cycle() == 0);
    mock_advance_to(1000);
    assert(cursor_save_failures == 1U && cursor_speed_save_work.due == 2000);
    mock_advance_to(1500);
    assert(bbp_cursor_speed_cycle() == 0);
    assert(cursor_save_failures == 0U && cursor_speed_save_work.due == 2500);
    mock_advance_to(2499);
    assert(mock_save_attempts == 1U);
    mock_save_error = 0;
    mock_advance_to(2500);
    assert(mock_save_attempts == 2U && mock_save_times[1] == 2500);
    assert(mock_flash_record[1] == 4U && !cursor_save_dirty);
}

static unsigned save_reentry_cycles;
static void change_during_backend_save(void) {
    assert(mock_lock_depth == 0U && cursor_save_running);
    for (unsigned n = 0U; n < save_reentry_cycles; ++n) {
        assert(bbp_cursor_speed_cycle() == 0);
    }
    /* Neither late load/commit nor a concurrent worker entry may replay/reset
     * state, start another backend, or deadlock while this save is in flight.
     */
    stage_index(0U);
    commit_settings();
    cursor_speed_save_worker(&cursor_speed_save_work.work);
    assert(mock_save_attempts == 1U);
}
static void test_reentrant_save_preserves_new_generation_and_budget(void) {
    for (unsigned scenario = 0U; scenario < 3U; ++scenario) {
        reset_boot(false);
        commit_settings();
        assert(bbp_cursor_speed_cycle() == 0); /* captured index 3, generation 1. */
        uint32_t captured = bbp_cursor_speed_snapshot();
        save_reentry_cycles = scenario == 0U ? 1U : 5U;
        mock_save_hook = change_during_backend_save;
        mock_save_error = scenario == 2U ? -EIO : 0;
        mock_advance_to(1000);
        uint32_t latest = bbp_cursor_speed_snapshot();
        uint8_t latest_index = scenario == 0U ? 4U : 3U;
        assert(current_index() == latest_index && latest != captured);
        assert(bbp_cursor_speed_snapshot_generation(latest) == 1U + save_reentry_cycles);
        assert(mock_save_attempts == 1U && cursor_save_dirty);
        assert(cursor_save_failures == 0U && cursor_speed_save_work.pending);
        assert(cursor_speed_save_work.due == 2000);
        if (scenario != 2U) {
            assert(mock_flash_present && mock_flash_record[1] == 3U);
        } else {
            assert(!mock_flash_present); /* Old-generation failure spent no new retries. */
        }
        mock_save_error = 0;
        mock_advance_to(1999);
        assert(mock_save_attempts == 1U);
        mock_advance_to(2000);
        assert(mock_save_attempts == 2U && mock_flash_record[1] == latest_index);
        assert(!cursor_save_dirty && !cursor_save_running && !cursor_speed_save_work.pending);
        mock_advance_to(100000);
        assert(mock_save_attempts == 2U && bbp_cursor_speed_snapshot() == latest);
    }
}
#endif /* CONFIG_SETTINGS */

static void behavior_edge(uint32_t position, bool down, int captured_layer) {
    struct zmk_behavior_binding binding = {.behavior_dev = "cursor_speed"};
    struct zmk_behavior_binding_event event = {
        .layer = captured_layer, .position = position, .timestamp = mock_now,
    };
    const struct behavior_driver_api *api = mock_cursor_behavior_api_0;
    int ret = down ? api->binding_pressed(&binding, event) : api->binding_released(&binding, event);
    assert(ret == ZMK_BEHAVIOR_OPAQUE);
    assert(mock_lock_depth == 0U);
}
static void test_behavior_duplicate_down_up_hold_and_layer_release(void) {
    reset_boot(false);
#if CONFIG_SETTINGS
    commit_settings();
#endif
    uint32_t initial = bbp_cursor_speed_snapshot();
    behavior_edge(28U, false, 2); /* Orphan/repeated UP never cycles. */
    assert(bbp_cursor_speed_snapshot() == initial);
    behavior_edge(28U, true, 2);
    uint32_t first = bbp_cursor_speed_snapshot();
    assert(first != initial && current_index() == 3U);
    for (unsigned n = 0U; n < 100U; ++n) { behavior_edge(28U, true, 2); }
    mock_advance_to(20000); /* Holding does not auto-repeat, including save work firing. */
    assert(bbp_cursor_speed_snapshot() == first);
    behavior_edge(27U, false, 0); /* Another position's UP cannot release this owner. */
    behavior_edge(28U, true, 0);
    assert(bbp_cursor_speed_snapshot() == first);
    behavior_edge(28U, false, 0); /* Captured position release after leaving UPPER. */
    behavior_edge(28U, false, 3);
    assert(bbp_cursor_speed_snapshot() == first && cursor_speed_held == 0U);
    behavior_edge(28U, true, 2);
    uint32_t second = bbp_cursor_speed_snapshot();
    assert(current_index() == 4U && bbp_cursor_speed_snapshot_generation(second) == 2U);
    behavior_edge(28U, false, 1);
    behavior_edge(28U, true, 2);
    assert(current_index() == 0U && bbp_cursor_speed_snapshot_generation(
        bbp_cursor_speed_snapshot()) == 3U);
    behavior_edge(28U, false, 2);
#if CONFIG_ZMK_BEHAVIOR_METADATA
    assert(mock_cursor_behavior_api_0->get_parameter_metadata == zmk_behavior_get_empty_param_metadata);
#else
    assert(mock_cursor_behavior_api_0->get_parameter_metadata == NULL);
#endif
}
static void test_behavior_all_42_positions_and_bounds(void) {
    reset_boot(false);
    for (uint32_t position = 0U; position < 42U; ++position) {
        behavior_edge(position, true, 2);
        behavior_edge(position, true, 2);
        assert((cursor_speed_held & (UINT64_C(1) << position)) != 0U);
        assert(bbp_cursor_speed_snapshot_generation(bbp_cursor_speed_snapshot()) == position + 1U);
    }
    assert(cursor_speed_held == ((UINT64_C(1) << 42U) - 1U));
    uint32_t all_down = bbp_cursor_speed_snapshot();
    for (uint32_t position = 0U; position < 42U; ++position) {
        behavior_edge(position, false, 0);
        behavior_edge(position, false, 3);
    }
    assert(cursor_speed_held == 0U && bbp_cursor_speed_snapshot() == all_down);
    const uint32_t invalid[] = {42U, 63U, 64U, UINT32_MAX};
    for (unsigned i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        behavior_edge(invalid[i], true, 2);
        behavior_edge(invalid[i], false, 0);
    }
    assert(cursor_speed_held == 0U && bbp_cursor_speed_snapshot() == all_down);
    behavior_edge(41U, true, 2);
    assert(bbp_cursor_speed_snapshot_generation(bbp_cursor_speed_snapshot()) == 43U);
    behavior_edge(41U, false, 0);
}

#if !CONFIG_SETTINGS
static void test_settings_off_is_volatile_without_work_or_backend(void) {
    reset_boot(false);
    for (unsigned frame = 0U; frame < 100000U; ++frame) {
        (void)bbp_cursor_speed_gain((frame & 1U) != 0U);
        (void)bbp_cursor_speed_snapshot();
        if (frame % 10000U == 0U) { assert(bbp_cursor_speed_cycle() == 0); }
    }
    assert(current_index() == 2U);
    assert(bbp_cursor_speed_snapshot_generation(bbp_cursor_speed_snapshot()) == 10U);
    mock_advance_to(1000000);
    assert(mock_scheduled_work == NULL && mock_work_runs == 0U);
    reset_boot(false);
    assert(bbp_cursor_speed_snapshot() == 2U && bbp_cursor_speed_gain(false) == 480U);
}
#endif

int main(void) {
    test_default_table_half_and_generation();
    test_motion_frame_linearization();
#if CONFIG_SETTINGS
    test_debounce_merge_and_premature_wakeup();
    test_fullcycle_same_saved_index_and_reboot();
    test_every_loaded_index_and_one_shot_commit();
    test_user_precedence_early_late_and_reentrant_read();
    test_invalid_unknown_short_and_negative_records();
    test_read_animation_and_movement_simulation_never_save();
    test_failure_budget_and_new_action_rearms();
    test_new_action_during_retry_restarts_debounce();
    test_reentrant_save_preserves_new_generation_and_budget();
#else
    test_settings_off_is_volatile_without_work_or_backend();
#endif
    test_behavior_duplicate_down_up_hold_and_layer_release();
    test_behavior_all_42_positions_and_bounds();
    puts(CONFIG_SETTINGS ?
         "PASS cursor speed actual service/behavior: gains, atomic generation, settings staging, debounce, retries, save reentry, captured releases" :
         "PASS cursor speed actual service/behavior: gains, atomic generation, settings-off volatile/no backend, duplicate/held/captured releases");
    return 0;
}
