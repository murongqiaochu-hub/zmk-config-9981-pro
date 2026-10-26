/* SPDX-License-Identifier: MIT -- isolated host-only prototype tests. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../experiments/gamepad_protocol/pure_hids.h"

static struct bt_conn peer[2];
static bool snapshot_ok, suspended[2];
static struct bbp_gamepad_report_body current;
static unsigned int events[6], suspend_calls;
static struct { enum bbp_exp_event event; uint16_t offset; int result; } last_event;
void bbp_exp_observe(enum bbp_exp_event event, uint16_t offset, int result) {
    assert((unsigned int)event < 6U); events[event]++;
    last_event.event=event; last_event.offset=offset; last_event.result=result;
}
bool bbp_exp_capture_report(struct bt_conn *conn, struct bbp_gamepad_report_body *out) {
    assert(conn == &peer[0] || conn == &peer[1]);
    *out = current;
    return snapshot_ok && conn == &peer[0];
}
void bbp_exp_set_suspended(struct bt_conn *conn, bool value) {
    assert(conn == &peer[0] || conn == &peer[1]);
    suspended[conn == &peer[1]] = value; suspend_calls++;
}
ssize_t bt_gatt_attr_read(struct bt_conn *c, const struct bt_gatt_attr *a, void *buf,
                          uint16_t len, uint16_t off, const void *value, uint16_t size) {
    (void)c; (void)a;
    if (off > size) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET); }
    size_t count = size - off;
    if (count > len) { count = len; }
    if (count) { memcpy(buf, (const uint8_t *)value + off, count); }
    return (ssize_t)count;
}
ssize_t bt_gatt_attr_read_service(struct bt_conn *c, const struct bt_gatt_attr *a,
                                  void *buf, uint16_t len, uint16_t off) {
    const struct bt_uuid *uuid = a->user_data;
    return bt_gatt_attr_read(c,a,buf,len,off,&uuid->val,2);
}
ssize_t bt_gatt_attr_read_chrc(struct bt_conn *c, const struct bt_gatt_attr *a,
                               void *buf, uint16_t len, uint16_t off) {
    const struct bt_gatt_chrc *chrc = a->user_data;
    return bt_gatt_attr_read(c,a,buf,len,off,&chrc->properties,1);
}
ssize_t bt_gatt_attr_read_ccc(struct bt_conn *c, const struct bt_gatt_attr *a,
                              void *buf, uint16_t len, uint16_t off) {
    (void)c; (void)a; (void)buf; (void)len; (void)off;
    /* SDK-owned CCC storage and subscription behavior are not modeled here. */
    return -ENOTSUP;
}
ssize_t bt_gatt_attr_write_ccc(struct bt_conn *c, const struct bt_gatt_attr *a,
                               const void *buf, uint16_t len, uint16_t off, uint8_t flags) {
    (void)c; (void)a; (void)buf; (void)len; (void)off; (void)flags;
    return -ENOTSUP;
}

static void test_structure(void) {
    const struct bt_gatt_attr *a = bbp_exp_hids.attrs;
    assert(bbp_exp_hids.attr_count == 16);
    assert(a[0].uuid->val == 0x2800 && ((struct bt_uuid *)a[0].user_data)->val == 0x1812);
    const unsigned int values[] = {2,4,6,8,12,15};
    const uint16_t uuids[] = {0x2a4e,0x2a4a,0x2a4b,0x2a4d,0x2a4d,0x2a4c};
    for (unsigned int i=0; i<6; ++i) {
        assert(a[values[i]].uuid->val == uuids[i]);
        assert(a[values[i]-1].uuid->val == 0x2803);
        if (a[values[i]].read) { assert(a[values[i]].perm & BT_GATT_PERM_READ_ENCRYPT); }
        if (a[values[i]].write) { assert(a[values[i]].perm & BT_GATT_PERM_WRITE_ENCRYPT); }
    }
    assert(a[8].read == bbp_exp_read_input && !a[8].write);
    const struct bt_gatt_chrc *chrc = a[7].user_data;
    assert(chrc->properties == (BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY));
    assert(a[9].uuid->val == 0x2902 && a[9].perm ==
           (BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT));
    assert(a[12].read == bbp_exp_read_output && a[12].write == bbp_exp_write_output);
    uint8_t ref[2];
    assert(a[10].read(&peer[0],a+10,ref,2,0)==2 && ref[0]==3 && ref[1]==1);
    assert(a[13].read(&peer[0],a+13,ref,2,0)==2 && ref[0]==5 && ref[1]==2);
}
static void test_map(void) {
    uint8_t out[256];
    const struct bt_gatt_attr *a = bbp_exp_hids.attrs + 6;
    for (unsigned int p=0; p<2; ++p) {
        for (size_t off=0; off<=sizeof(bbp_exp_report_map)+1; ++off) {
            memset(out,0xa5,sizeof(out));
            ssize_t got=a->read(&peer[p],a,out,22,(uint16_t)off);
            if (off>sizeof(bbp_exp_report_map)) { assert(got==-BT_ATT_ERR_INVALID_OFFSET); }
            else {
                size_t count=sizeof(bbp_exp_report_map)-off;
                if(count>22) { count=22; }
                assert(got==(ssize_t)count);
                assert(!memcmp(out,bbp_exp_report_map+off,count));
                assert(out[count]==0xa5);
            }
        }
    }
}
static void test_read_boundaries(void) {
    const unsigned int indexes[]={2,4,6,8,10,12,13};
    const uint16_t sizes[]={1,4,sizeof(bbp_exp_report_map),10,2,4,2};
    const uint16_t lengths[]={0,1,3,255};
    uint8_t expected[256],out[256];
    current=bbp_gamepad_neutral(); snapshot_ok=true;
    for(unsigned int i=0;i<7;++i) {
        const struct bt_gatt_attr *a=bbp_exp_hids.attrs+indexes[i];
        assert(a->read(&peer[0],a,expected,255,0)==sizes[i]);
        for(uint16_t off=0;off<=sizes[i]+1;++off) {
            for(unsigned int j=0;j<4;++j) {
                memset(out,0xa5,sizeof(out));
                ssize_t got=a->read(&peer[0],a,out,lengths[j],off);
                if(off>sizes[i]) { assert(got==-BT_ATT_ERR_INVALID_OFFSET && out[0]==0xa5); }
                else {
                    size_t count=sizes[i]-off;
                    if(count>lengths[j]) { count=lengths[j]; }
                    assert(got==(ssize_t)count && !memcmp(out,expected+off,count));
                    assert(out[count]==0xa5);
                }
                if(indexes[i]==6 || indexes[i]==8) {
                    assert(last_event.event==(indexes[i]==6?BBP_EXP_MAP_READ:BBP_EXP_INPUT_READ));
                    assert(last_event.offset==off && last_event.result==got);
                }
            }
        }
        assert(a->read(&peer[0],a,out,255,UINT16_MAX)==-BT_ATT_ERR_INVALID_OFFSET);
    }
}
static void test_input_and_controls(void) {
    const struct bt_gatt_attr *a=bbp_exp_hids.attrs;
    uint8_t out[16], expected[10], zero[4]={0};
    current=bbp_gamepad_neutral(); current.buttons_lo=1; current.x=127; current.lt=255;
    snapshot_ok=true;
    assert(bbp_experiment_stadia_encode(&current,expected));
    assert(a[8].read(&peer[0],a+8,out,16,0)==10 && !memcmp(out,expected,10));
    assert(a[8].read(&peer[0],a+8,out,16,7)==3 && !memcmp(out,expected+7,3));
    const uint8_t neutral[10]={15,0,0,128,128,128,128,0,0,0};
    assert(a[8].read(&peer[1],a+8,out,16,0)==10 && !memcmp(out,neutral,10));
    for(unsigned int invalid=0;invalid<5;++invalid) {
        current=bbp_gamepad_neutral();
        if(invalid==0) { current.hat=9; }
        if(invalid==1) { current.x=INT8_MIN; }
        if(invalid==2) { current.y=INT8_MIN; }
        if(invalid==3) { current.z=INT8_MIN; }
        if(invalid==4) { current.rz=INT8_MIN; }
        assert(a[8].read(&peer[0],a+8,out,16,0)==10 && !memcmp(out,neutral,10));
    }
    snapshot_ok=false;
    assert(a[8].read(&peer[0],a+8,out,16,0)==10 && !memcmp(out,neutral,10));
    assert(a[8].read(NULL,a+8,out,16,0)==10 && !memcmp(out,neutral,10));
    assert(a[8].read(&peer[0],a+8,out,16,11)==-BT_ATT_ERR_INVALID_OFFSET);
    for(uint16_t n=0;n<=6;++n) {
        assert(a[12].write(&peer[0],a+12,zero,n,0,0)==(n==4?4:-BT_ATT_ERR_INVALID_ATTRIBUTE_LEN));
    }
    assert(a[12].write(&peer[0],a+12,zero,4,1,0)==-BT_ATT_ERR_INVALID_OFFSET);
    assert(a[12].read(&peer[1],a+12,out,16,0)==4 && !memcmp(out,zero,4));
    for(uint16_t v=0;v<256;++v) {
        uint8_t value=(uint8_t)v;
        assert(a[2].write(&peer[0],a+2,&value,1,0,0)==(v==1?1:-0x13));
        unsigned int calls=suspend_calls;
        assert(a[15].write(&peer[0],a+15,&value,1,0,0)==(v<=1?1:-0x13));
        assert(suspend_calls==calls+(v<=1));
    }
    uint8_t suspend=0,resume=1;
    assert(a[15].write(&peer[0],a+15,&suspend,1,0,0)==1 && suspended[0] && !suspended[1]);
    assert(a[15].write(&peer[1],a+15,&resume,1,0,0)==1 && suspended[0] && !suspended[1]);
    assert(a[15].write(NULL,a+15,&resume,1,0,0)==-BT_ATT_ERR_UNLIKELY);
    assert(a[15].write(&peer[0],a+15,NULL,0,0,0)==-BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    assert(a[2].write(&peer[0],a+2,NULL,0,0,0)==-BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    assert(a[2].write(&peer[0],a+2,&resume,1,1,0)==-BT_ATT_ERR_INVALID_OFFSET);
    assert(a[2].read(&peer[0],a+2,out,16,0)==1 && out[0]==1);
    assert(a[2].write(&peer[0],a+2,NULL,1,0,0)==-BT_ATT_ERR_UNLIKELY);
    assert(a[15].write(&peer[0],a+15,NULL,1,0,0)==-BT_ATT_ERR_UNLIKELY);
    assert(a[12].write(&peer[0],a+12,NULL,4,0,0)==-BT_ATT_ERR_UNLIKELY);
    for(unsigned int flags=0;flags<256;++flags) {
        bool allowed=flags==0 || flags==2;
        unsigned int calls=suspend_calls;
        assert(a[15].write(&peer[0],a+15,&suspend,1,0,(uint8_t)flags)==(allowed?1:-0x06));
        assert(suspend_calls==calls+allowed);
        assert(a[2].write(&peer[0],a+2,&resume,1,0,(uint8_t)flags)==(allowed?1:-0x06));
        assert(a[12].write(&peer[0],a+12,zero,4,0,(uint8_t)flags)==(allowed?4:-0x06));
    }
    bbp_exp_ccc_changed(a+9,1);
    for(unsigned int i=0;i<6;++i) { assert(events[i]>0); }
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--descriptor")) {
        return fwrite(bbp_exp_report_map,1,sizeof(bbp_exp_report_map),stdout)==sizeof(bbp_exp_report_map)?0:1;
    }
    if(argc==2 && !strcmp(argv[1],"--sample")) {
        current=bbp_gamepad_neutral(); current.buttons_lo=1; current.x=127; current.lt=255;
        snapshot_ok=true;
        uint8_t sample[10];
        const struct bt_gatt_attr *a=bbp_exp_hids.attrs+8;
        assert(a->read(&peer[0],a,sample,10,0)==10);
        return fwrite(sample,1,10,stdout)==10?0:1;
    }
    if(argc==2 && !strcmp(argv[1],"--refs")) {
        for(size_t i=0;i<bbp_exp_hids.attr_count;++i) {
            const struct bt_gatt_attr *a=bbp_exp_hids.attrs+i;
            if(a->uuid->val==0x2908) {
                uint8_t ref[2]; assert(a->read(&peer[0],a,ref,2,0)==2);
                printf("%u %u\n",ref[0],ref[1]);
            }
        }
        return 0;
    }
    test_structure(); test_map(); test_read_boundaries(); test_input_and_controls();
    puts("PASS isolated pure HIDS: encrypted 16-attr table, immutable Map, input10/output4, peer-neutral reads, control boundaries and metadata-only diagnostics; no notification/radio/OS test");
    return 0;
}
