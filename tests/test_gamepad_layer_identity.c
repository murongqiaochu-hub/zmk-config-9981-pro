/* MEDIA must be recognised by stable layer ID, not by keymap order index.
 * Studio/users may reorder layers, in which case the pinned SDK returns an
 * index whose numeric value is not the stable ID. This test drives the REAL
 * runtime predicate with a reordered mapping; the same test must FAIL when
 * compiled against the previous index comparison.
 */
#include <stdio.h>
#include "gamepad_runtime_env.h"
#include "../config/boards/arm/bbp9981/custom_driver/gamepad_runtime.c"

int tp_tap_output_cancel(void) { return 0; }
uint32_t bbp9981_usb_hid_connection_epoch(void) { return 0U; }
int bbp_gamepad_usb_send(const struct bbp_gamepad_report_body *body, uint32_t epoch,
                         uint32_t context) {
    (void)body; (void)epoch; (void)context;
    return 0;
}
int bbp_gamepad_ble_send(struct bt_conn *conn, const struct bbp_gamepad_report_body *body,
                         uint32_t context) {
    (void)conn; (void)body; (void)context;
    return 0;
}

static void set_order(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    bbp_test_layer_order[0] = a;
    bbp_test_layer_order[1] = b;
    bbp_test_layer_order[2] = c;
    bbp_test_layer_order[3] = d;
}

int main(void) {
    static const uint8_t media = BBP_GAMEPAD_LAYER;
    set_order(0, 1, 2, 3);
    highest_layer = media;
    assert(bbp_gamepad_layer_active());

    /* Swapped stable IDs 2 and 3: the active index 2 now means layer ID 3. */
    set_order(0, 1, media, 2);
    highest_layer = 2;
    assert(bbp_gamepad_layer_active());
    /* ... and the reverse: index 2 holds MEDIA only under the identity order. */
    highest_layer = media;
    assert(!bbp_gamepad_layer_active());

    set_order(0, 1, 2, 3);
    highest_layer = media;
    assert(bbp_gamepad_layer_active());
    highest_layer = 0;
    assert(!bbp_gamepad_layer_active());
    printf("PASS MEDIA identity: stable layer ID survives keymap reordering\n");
    return 0;
}
