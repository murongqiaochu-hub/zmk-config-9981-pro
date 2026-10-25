/* SPDX-License-Identifier: MIT -- original real-HID oracles plus observers. */
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/bluetooth/gatt.h>
static unsigned observed_maps, observed_refs, observed_notifies;
static uint16_t last_total,last_offset,last_requested;
static ssize_t last_result;
void bbp_ipdiag_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                    uint16_t requested, uint16_t offset, const void *value,
                    uint16_t total, ssize_t result, bool map) {
    (void)conn;(void)value;
    if(map) { observed_maps++;last_total=total;last_offset=offset;last_requested=requested;last_result=result; }
    else if(attr && attr->uuid->val==BT_UUID_HIDS_REPORT_REF->val && total==2) { observed_refs++; }
}
int bbp_ipdiag_notify(struct bt_conn *conn, struct bt_gatt_notify_params *params) {
    observed_notifies++;
    return bt_gatt_notify_cb(conn,params);
}
#include "test_hid_metadata.c"
int main(void) {
    char *args[]={"test",NULL};
    assert(metadata_main(1,args)==0);
    reset_case();
    uint8_t out[64];
    assert(hog_svc.attrs[4].read(&peers[0],&hog_svc.attrs[4],out,64,320)==17);
#if CONFIG_BBP9981_IPAD_DIAG
    assert(observed_maps>1000 && observed_refs>0 && observed_notifies>0);
    assert(last_total==337 && last_offset==320 && last_requested==64 && last_result==17);
    unsigned before_notify=observed_notifies;
    test_original_stock_senders_remain();
    assert(observed_notifies==before_notify+4); /* keyboard, consumer, mouse, keyboard error */
    before_notify=observed_notifies;
    test_existing_mouse_snapshot_uses_original_index13();
    assert(observed_notifies==before_notify+2); /* both actual custom mouse-worker calls */
    unsigned old=observed_refs;
    for(unsigned i=0;i<5;i++) {
        const unsigned indices[]={8,12,16,19,25};
        const struct bt_gatt_attr *attr=&hog_svc.attrs[indices[i]];
        assert(attr->read(&peers[0],attr,out,2,0)==2);
    }
    assert(observed_refs==old+5);
    assert(hog_svc.attrs[4].read(&peers[0],&hog_svc.attrs[4],out,64,338)==BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET));
    assert(last_result==BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET)&&last_total==337);
#else
    assert(!observed_maps&&!observed_refs&&!observed_notifies);
#endif
    puts("PASS IPDIAG HOG ON/OFF: real337 map and five refs, original senders/snapshot, untouched legacy oracles");
}
