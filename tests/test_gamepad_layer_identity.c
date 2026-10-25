/* Exercise the production runtime, not a mocked game-layer predicate.
 * This models the fixed SDK's stable IDs / reorderable indices contract;
 * it does not establish that any physical device has reordered its layers. */
#define main gamepad_runtime_original_main
#include "test_gamepad_runtime.c"
#undef main

static void reordered_layers(void) {
    /* Original IDs: DEFAULT=0, LOWER=1, RAISE=2, MEDIA=3. */
    const zmk_keymap_layer_id_t order[] = {1, 3, 2, 0};
    memcpy(layer_order, order, sizeof(order));
}

static void assert_ordinary_inputs_ignored(void) {
    unsigned frames = accepted_count;
    edge(24, BBP_GP_A, true);
    motion(8, -4);
    assert(!count && !state_destination_valid);
    run(2, 0);
    /* Idle neutral handshake is allowed, but never game input. */
    for (unsigned i = frames; i < accepted_count; i++) {
        assert(bbp_gamepad_is_neutral(&accepted[i].body));
    }
    struct bbp_gamepad_report_body report;
    bbp_gamepad_get_report(0, &report);
    assert(bbp_gamepad_is_neutral(&report));
    assert(!last_valid && !pending_valid);
}

static void identity_order(void) {
    fixture(false);
    for (unsigned i = 0; i < 4; i++) {
        highest_layer = (zmk_keymap_layer_index_t)i;
        assert(bbp_gamepad_layer_active() == (i == BBP_GAMEPAD_LAYER));
    }
    no_leaks();
}

static void default_at_index_three(void) {
    fixture(false);
    reordered_layers();
    highest_layer = 3;
    assert(layer_order[highest_layer] == 0);
    assert(!bbp_gamepad_layer_active());
    uint32_t pointer_epoch = bbp_gamepad_pointer_epoch();
    uint32_t context = bbp_gamepad_context_epoch();
    /* Ordinary layer/endpoint/USB/profile events all use this real listener. */
    for (unsigned i = 0; i < 4; i++) {
        assert(context_changed(NULL) == ZMK_EV_EVENT_BUBBLE);
        assert(tap_bridge_cancels == 0);
        assert(bbp_gamepad_pointer_epoch() == pointer_epoch);
    }
    /* Transport invalidation remains intentional, distinct from pointer mode. */
    assert(bbp_gamepad_context_epoch() == context + 4);
    assert_ordinary_inputs_ignored();
    no_leaks();
}

static void media_at_index_one(void) {
    fixture(false);
    reordered_layers();
    highest_layer = 3;
    context_changed(NULL);
    uint32_t pointer_epoch = bbp_gamepad_pointer_epoch();
    highest_layer = 1;
    assert(bbp_gamepad_layer_active());
    context_changed(NULL);
    assert(tap_bridge_cancels == 1);
    assert(bbp_gamepad_pointer_epoch() == pointer_epoch + 1);
    edge(24, BBP_GP_A, true);
    motion(8, -4);
    run(1, 0);
    assert(last_valid && last.body.buttons_lo == 1);
    assert(last.body.x == 48 && last.body.y == -24);
    struct bbp_gamepad_report_body report;
    bbp_gamepad_get_report(0, &report);
    assert(report.buttons_lo == 1 && report.x == 48 && report.y == -24);
    highest_layer = 3;
    context_changed(NULL);
    assert(!bbp_gamepad_layer_active());
    assert(tap_bridge_cancels == 2);
    assert(bbp_gamepad_pointer_epoch() == pointer_epoch + 2);
    unsigned frames = accepted_count;
    run(8, 10);
    assert(accepted_count > frames);
    for (unsigned i = frames; i < accepted_count; i++) {
        assert(bbp_gamepad_is_neutral(&accepted[i].body));
    }
    assert(!count && !last_valid && !pending_valid);
    context_changed(NULL);
    assert(tap_bridge_cancels == 2);
    assert(bbp_gamepad_pointer_epoch() == pointer_epoch + 2);
    no_leaks();
}

static void non_media_and_invalid(void) {
    fixture(false);
    reordered_layers();
    const zmk_keymap_layer_index_t ordinary[] = {0, 2, 3, 4, UINT8_MAX};
    uint32_t pointer_epoch = bbp_gamepad_pointer_epoch();
    for (unsigned i = 0; i < sizeof(ordinary) / sizeof(ordinary[0]); i++) {
        highest_layer = ordinary[i];
        assert(!bbp_gamepad_layer_active());
        context_changed(NULL);
        assert(tap_bridge_cancels == 0);
        assert(bbp_gamepad_pointer_epoch() == pointer_epoch);
        assert_ordinary_inputs_ignored();
    }
    /* Also cover an SDK mapping result of INVAL at the old magic index 3. */
    highest_layer = 3;
    layer_order[3] = ZMK_KEYMAP_LAYER_ID_INVAL;
    assert(!bbp_gamepad_layer_active());
    context_changed(NULL);
    assert(tap_bridge_cancels == 0);
    assert(bbp_gamepad_pointer_epoch() == pointer_epoch);
    assert_ordinary_inputs_ignored();
    no_leaks();
}

int main(void) {
    identity_order();
    default_at_index_three();
    media_at_index_one();
    non_media_and_invalid();
    /* Fixture reset must restore identity ordering for every original assertion. */
    assert(gamepad_runtime_original_main() == 0);
    puts("PASS actual runtime layer identity: reordered DEFAULT stays ordinary, MEDIA follows stable ID, invalid/non-MEDIA ignored, ordinary context events preserve tap/pointer ownership; host only");
    return 0;
}
