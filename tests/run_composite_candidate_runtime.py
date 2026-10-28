#!/usr/bin/env python3
"""Host-link the actual runtime and composite candidate with pinned HOG.

Only temporary includes are generated; the 45B HID mock is copied with its map
replaced by the verified stock196 fixture. The linked runtime source is the
unchanged production file. Scheduler, CCC, ATT, peers and USB remain MOCKS.
"""
import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

sys.dont_write_bytecode=True
from run_composite_hog_host import check_fixed, ROOT
from run_composite_hog_snapshot_host import verify_snapshot
from run_composite_ble_candidate import fixture_copy

PINNED={
    'config/boards/arm/bbp9981/custom_driver/gamepad_runtime.c': '6ff0a82b94ec61f9086af87e3a2c5913d494aa53961724474f2487f6455d169f',
    'config/boards/arm/bbp9981/custom_driver/gamepad_runtime.h': 'c121ecf97198be5cfd1a24a37de232b9c76f92c57883e588d0dca02875d0ca3b',
    'config/boards/arm/bbp9981/custom_driver/gamepad_report.h': '449404b7e21d894de8ce28bbea5b907d2dc887c02ccfe3944b7e108a498fcbd8',
    'config/boards/arm/bbp9981/custom_driver/gamepad_stick.h': 'ce9575311c7a2b971bdb9bd859711e0f9ebeae04d4a46c46909cef0e466ad1ba',
}

def check_runtime():
    for name, expected in PINNED.items():
        actual=hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
        if actual!=expected: raise AssertionError(f'fixed runtime source changed: {name}: {actual}')

def make_runtime_shims(root):
    empty=['zephyr/device.h','zmk/events/layer_state_changed.h','zmk/events/endpoint_changed.h',
           'zmk/events/usb_conn_state_changed.h','zmk/events/ble_active_profile_changed.h']
    content={name:'#pragma once\n/* host-only empty declaration boundary */\n' for name in empty}
    content['zephyr/sys/atomic.h']='''#pragma once
#include <stdbool.h>
typedef int atomic_t;
static inline int atomic_get(const atomic_t *a){return *a;}
static inline int atomic_inc(atomic_t *a){return (*a)++;}
static inline int atomic_set(atomic_t *a,int n){int old=*a;*a=n;return old;}
static inline bool atomic_cas(atomic_t *a,int old,int n){if(*a!=old)return false;*a=n;return true;}
'''
    content['zmk/event_manager.h']='''#pragma once
typedef struct { int unused; } zmk_event_t;
#define ZMK_LISTENER(...)
#define ZMK_SUBSCRIPTION(...)
#define ZMK_EV_EVENT_BUBBLE 0
'''
    content['zmk/keymap.h']='''#pragma once
#include <stdint.h>
uint8_t zmk_keymap_highest_layer_active(void);
uint8_t zmk_keymap_layer_index_to_id(uint8_t index);
'''
    content['zmk/usb.h']='''#pragma once
#include <stdbool.h>
bool zmk_usb_is_hid_ready(void);
'''
    for name,data in content.items():
        path=root/name; path.parent.mkdir(parents=True,exist_ok=True)
        assert path.resolve().is_relative_to(root.resolve())
        path.write_text(data)

def separate_tu_test(temp,overlay,mocks,ccflags,config,prelude,snapshot,candidate,sanitizer):
    """Compile actual HOG and actual runtime separately, then link a mock driver.

    Reuse the previous host boundary functions verbatim, omitting its FAKE
    runtime functions and test cases. Generated C files stay in the temp dir.
    """
    runtime=ROOT/'config/boards/arm/bbp9981/custom_driver/gamepad_runtime.c'
    runtime_header=ROOT/'config/boards/arm/bbp9981/custom_driver/gamepad_runtime.h'
    hog=ROOT/'tests/gamepad_ble_mocks/sdk_v0_3_0_hog.c'
    previous=(ROOT/'tests/test_composite_ble_candidate.c').read_text()
    body=previous[previous.index('#define VALUE 26U'):previous.index('k_spinlock_key_t k_spin_lock(')]
    first=body.index('uint32_t bbp_gamepad_context_epoch(void)')
    last=body.index('struct zmk_hid_keyboard_report *zmk_hid_get_keyboard_report',first)
    body=body[:first]+body[last:] # NEVER link the prior fake context/get_report.
    notify='int bt_gatt_notify_cb(struct bt_conn *c,struct bt_gatt_notify_params *p) {'
    assert body.count(notify)==1
    body=body.replace(notify,'static int fixture_link_notify(struct bt_conn *c,struct bt_gatt_notify_params *p) {',1)
    runtime_pre=temp/'runtime-separate-prelude.h'
    runtime_pre.write_text('''#include <zephyr/kernel.h>
#include <zephyr/bluetooth/conn.h>
#include <stdbool.h>
enum bt_security_err { BT_SECURITY_ERR_SUCCESS };
struct bt_conn_cb {
    void (*connected)(struct bt_conn *,uint8_t);
    void (*disconnected)(struct bt_conn *,uint8_t);
    void (*security_changed)(struct bt_conn *,bt_security_t,enum bt_security_err);
};
#define BT_CONN_CB_DEFINE(name) struct bt_conn_cb name
#define SYS_INIT(fn,level,priority) int (*runtime_link_init)(void)=(fn)
#define APPLICATION 0
#define LOG_MODULE_REGISTER(...)
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) ((void)0)
int k_work_reschedule_for_queue(struct k_work_q *,struct k_work_delayable *,k_timeout_t);
''')
    hog_unit=temp/'hog-separate.c'
    hog_unit.write_text('#include "'+str(hog)+'"\n')
    glue=temp/'separate-link-mock.c'
    glue.write_text('''#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/gatt.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>
#include "'''+str(runtime_header)+'''"
extern const struct bt_gatt_service_static hog_svc;
extern struct k_work_q hog_work_q;
extern int (*gamepad_mock_hog_init)(void);
extern int (*runtime_link_init)(void);
'''+body+'''
static struct k_work_delayable *scheduled;
static unsigned int usb_sent_count;
int bt_gatt_notify_cb(struct bt_conn *c,struct bt_gatt_notify_params *p) {
    /* Extra host mock check for ENFORCE_SUBSCRIPTION; not a real SDK test. */
    if(p && p->attr==&hog_svc.attrs[VALUE] &&
       !bt_gatt_is_subscribed(c,p->attr,BT_GATT_CCC_NOTIFY)) return -EINVAL;
    return fixture_link_notify(c,p);
}
k_spinlock_key_t k_spin_lock(struct k_spinlock *l) {
    assert(l && !l->held && !lock_depth); l->held=true; return lock_depth++;
}
void k_spin_unlock(struct k_spinlock *l,k_spinlock_key_t key) {
    assert(l && l->held && lock_depth==key+1U); l->held=false; --lock_depth;
}
int64_t k_uptime_get(void) { return 100; }
int k_msgq_put(struct k_msgq *q,const void *d,k_timeout_t t) {
    assert(q && d && (t==K_NO_WAIT || t==K_MSEC(100)));
    if(q->used==q->capacity) return -EAGAIN;
    memcpy(q->buffer+q->write_pos*q->item_size,d,q->item_size);
    q->write_pos=(q->write_pos+1)%q->capacity; ++q->used; return 0;
}
int k_msgq_get(struct k_msgq *q,void *d,k_timeout_t t) {
    assert(q && d && t==K_NO_WAIT); if(!q->used) return -ENOMSG;
    memcpy(d,q->buffer+q->read_pos*q->item_size,q->item_size);
    q->read_pos=(q->read_pos+1)%q->capacity; --q->used; return 0;
}
int k_msgq_peek(struct k_msgq *q,void *d) {
    assert(q && d); if(!q->used) return -ENOMSG;
    memcpy(d,q->buffer+q->read_pos*q->item_size,q->item_size); return 0;
}
unsigned int k_msgq_num_used_get(struct k_msgq *q) { return q->used; }
int k_work_submit_to_queue(struct k_work_q *q,struct k_work *w) {
    assert(q && q->started && w); return 1;
}
int k_work_schedule_for_queue(struct k_work_q *q,struct k_work_delayable *w,k_timeout_t d) {
    assert(q && q->started && w && d>=0); scheduled=w; return 1;
}
int k_work_reschedule_for_queue(struct k_work_q *q,struct k_work_delayable *w,k_timeout_t d) {
    assert(q && q->started && w && d>=0); scheduled=w; return 1;
}
void k_work_queue_start(struct k_work_q *q,void *stack,size_t size,int priority,
                        const struct k_work_queue_config *config) {
    (void)priority; assert(q && stack && size && config); q->started=true;
}
uint8_t zmk_keymap_highest_layer_active(void) { return BBP_GAMEPAD_LAYER; }
uint8_t zmk_keymap_layer_index_to_id(uint8_t index) { return index; }
bool zmk_usb_is_hid_ready(void) { return true; }
uint32_t bbp9981_usb_hid_connection_epoch(void) { return 1; }
int tp_tap_output_cancel(void) { return 0; }
int bbp_gamepad_usb_send(const struct bbp_gamepad_report_body *b,uint32_t u,uint32_t e) {
    (void)e; assert(b && u==1 && sizeof(*b)==9); ++usb_sent_count; return 0;
}
int main(void) {
    for(size_t i=0;i<4;++i) {
        peer[i]=(struct bt_conn){.state=BT_CONN_STATE_CONNECTED,.refs=1};
        peer[i].dst.type=1; peer[i].dst.a.val[5]=(uint8_t)i;
    }
    active=&peer[0]; profile=0;
    endpoint=(struct zmk_endpoint_instance){.transport=ZMK_TRANSPORT_BLE,.ble={.profile_index=0}};
    assert(gamepad_mock_hog_init && runtime_link_init);
    assert(gamepad_mock_hog_init()==0 && runtime_link_init()==0);
    uint8_t on[2]={1,0}; uint32_t before=bbp_gamepad_context_epoch();
    assert(hog_svc.attrs[27].write(&peer[0],hog_svc.attrs+27,on,2,0,0)==2);
    assert(bbp_gamepad_context_epoch()!=before);
    assert(bbp_gamepad_key_event(24,BBP_GP_A,true,100)==0 && scheduled);
    struct k_work_delayable *w=scheduled; scheduled=NULL; w->work.handler(&w->work);
    assert(sent_count==2 && sent[0].attr==hog_svc.attrs+26 && sent[0].len==10 && sent[0].data[0]==15);
    assert(sent[1].conn==&peer[0] && sent[1].len==10 && sent[1].data[2]==64 && peer[0].refs==2);
    endpoint.transport=ZMK_TRANSPORT_USB; bbp_gamepad_transport_invalidate(); assert(scheduled);
    w=scheduled; scheduled=NULL; w->work.handler(&w->work);
    assert(sent_count>=3 && sent[2].conn==&peer[0] && sent[2].data[0]==15 && sent[2].data[2]==0);
    assert(peer[0].refs==1 && usb_sent_count>=1 && refs==unrefs && !lock_depth);
    puts("PASS separate real runtime.o + HOG/candidate.o: init, CCC, worker, old-peer neutral and USB mock");
    return 0;
}
''')
    san=['-fsanitize='+sanitizer]
    if sanitizer=='undefined': san+=['-fno-sanitize-recover=all']
    inc=['-I'+str(overlay),'-I'+str(mocks)]
    objects=[temp/('runtime-tu-'+sanitizer+'.o'),temp/('hog-tu-'+sanitizer+'.o'),
             temp/('mock-tu-'+sanitizer+'.o')]
    subprocess.run([*ccflags,*config,*inc,'-include',str(runtime_pre),*san,'-c',str(runtime),'-o',str(objects[0])],check=True)
    subprocess.run([*ccflags,*config,*inc,'-include',str(prelude),'-include',str(snapshot),
                    '-include',str(candidate),*san,'-c',str(hog_unit),'-o',str(objects[1])],check=True)
    subprocess.run([*ccflags,'-Wno-unused-variable',*config,*inc,*san,'-c',str(glue),'-o',str(objects[2])],check=True)
    stock_hog=temp/('stock-hog-'+sanitizer+'.o')
    subprocess.run([*ccflags,*config,*inc,'-include',str(prelude),'-include',str(snapshot),
                    *san,'-c',str(hog_unit),'-o',str(stock_hog)],check=True)
    # Stronger negative: the actual stock HOG and snapshot are present, but the
    # candidate ABI is absent. A link failure must name bbp_gamepad_ble_send.
    negative=subprocess.run([*ccflags,*san,str(objects[0]),str(stock_hog),str(objects[2]),
                             '-o',str(temp/'missing-candidate')],
                            stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,
                            env={**os.environ,'LC_ALL':'C'})
    missing=re.findall(r"undefined reference to [`'‘]([^'’`]+)['’`]",negative.stderr)
    assert negative.returncode!=0 and missing and set(missing)=={'bbp_gamepad_ble_send'}, negative.stderr
    exe=temp/('separate-tu-'+sanitizer)
    subprocess.run([*ccflags,*san,*map(str,objects),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
    print('PASS '+sanitizer+' separate objects; stock HOG without candidate rejects unresolved BLE send',flush=True)


def main():
    if not __debug__: raise RuntimeError('the oracle must not run under Python -O')
    stock=check_fixed(); verify_snapshot(); check_runtime()
    candidate=ROOT/'experiments/gamepad_protocol/composite_ble_candidate.h'
    candidate_fixture=ROOT/'tests/test_composite_ble_candidate.c'
    candidate_sha=hashlib.sha256(candidate.read_bytes()).hexdigest()
    fixture_sha=hashlib.sha256(candidate_fixture.read_bytes()).hexdigest()
    cc=shlex.split(os.environ.get('CC','cc')); assert cc
    source=ROOT/'tests/test_composite_candidate_runtime.c'
    snapshot=ROOT/'config/boards/arm/bbp9981/custom_driver/trackpad_hog_snapshot.h'
    mocks=ROOT/'tests/gamepad_ble_mocks'
    flags=[*cc,'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
           '-Wno-unused-function','-fno-omit-frame-pointer','-fno-pie','-no-pie']
    config=['-DBBP9981_COMPOSITE_EXPERIMENT=1','-DCONFIG_ZMK_POINTING=1',
            '-DCONFIG_ZMK_POINTING_SMOOTH_SCROLLING=0','-DCONFIG_ZMK_HID_INDICATORS=1',
            '-DCONFIG_ZMK_BLE_THREAD_STACK_SIZE=512','-DCONFIG_ZMK_BLE_THREAD_PRIORITY=5',
            '-DCONFIG_ZMK_BLE_INIT_PRIORITY=50','-DCONFIG_ZMK_BLE_KEYBOARD_REPORT_QUEUE_SIZE=20',
            '-DCONFIG_ZMK_BLE_CONSUMER_REPORT_QUEUE_SIZE=5','-DCONFIG_ZMK_BLE_MOUSE_REPORT_QUEUE_SIZE=20',
            '-DCONFIG_ZMK_LOG_LEVEL=0','-DCONFIG_A320_CURSOR_STEADY=1','-DCONFIG_A320_TAP_OUTPUT=1',
            '-DCONFIG_BBP9981_MENU_DIAG=1','-DCONFIG_ZMK_STUDIO=1','-DCONFIG_ZMK_USB=1',
            '-DCONFIG_ZMK_BLE=1','-DCONFIG_BT_GATT_ENFORCE_SUBSCRIPTION=1',
            '-DCONFIG_APPLICATION_INIT_PRIORITY=50']
    with tempfile.TemporaryDirectory(prefix='9981-runtime-composite-') as directory:
        temp=Path(directory)
        assert temp.resolve().parent==Path(tempfile.gettempdir()).resolve()
        assert temp.name.startswith('9981-runtime-composite-') and not temp.is_symlink()
        overlay=temp/'include'; fixture_copy(overlay,stock); make_runtime_shims(overlay)
        prelude=temp/'peek.h'
        prelude.write_text('#include <zephyr/kernel.h>\nint k_msgq_peek(struct k_msgq *, void *);\n')
        includes=['-I'+str(overlay),'-I'+str(mocks),'-include',str(prelude),
                  '-include',str(snapshot),'-include',str(candidate)]
        for sanitizer in ('undefined','address'):
            exe=temp/('runtime-'+sanitizer)
            san=['-fsanitize='+sanitizer]
            if sanitizer=='undefined': san+=['-fno-sanitize-recover=all']
            print('RUN actual runtime + candidate + HOG: '+sanitizer,flush=True)
            subprocess.run([*flags,*config,*includes,*san,str(source),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
            separate_tu_test(temp,overlay,mocks,flags,config,prelude,snapshot,candidate,sanitizer)
        assert check_fixed()==stock; verify_snapshot(); check_runtime()
        assert hashlib.sha256(candidate.read_bytes()).hexdigest()==candidate_sha
        assert hashlib.sha256(candidate_fixture.read_bytes()).hexdigest()==fixture_sha
    print('PASS actual runtime source SHA '+PINNED['config/boards/arm/bbp9981/custom_driver/gamepad_runtime.c'])
    print('LIMIT mock time/worker dispatch, CCC/ATT/peers and USB; no real Zephyr, ARM, OS or device')

if __name__=='__main__':
    main()
