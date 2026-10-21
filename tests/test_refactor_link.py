#!/usr/bin/env python3
"""Compile production gamepad TUs separately and link their real adapter API.
Mocks cover six transport/tap combinations; real Zephyr linking remains an ARM gate.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
from refactor_support import ROOT, DRIVER

PREFIX = r'''
#pragma once
#include "gamepad_runtime_env.h"
#undef CONFIG_ZMK_USB
#undef CONFIG_ZMK_BLE
#undef CONFIG_A320_TAP_OUTPUT
#define CONFIG_ZMK_USB TEST_USB
#define CONFIG_ZMK_BLE TEST_BLE
#define CONFIG_A320_TAP_OUTPUT TEST_TAP
#undef DT_HAS_COMPAT_STATUS_OKAY
#define DT_HAS_COMPAT_STATUS_OKAY(x) 1
#define POST_KERNEL 0
#define CONFIG_KERNEL_INIT_PRIORITY_DEFAULT 40
#define DT_INST_FOREACH_STATUS_OKAY(fn) fn(0)
struct behavior_driver_api {
 int (*binding_pressed)(struct zmk_behavior_binding*,struct zmk_behavior_binding_event);
 int (*binding_released)(struct zmk_behavior_binding*,struct zmk_behavior_binding_event);
};
#define BEHAVIOR_DT_INST_DEFINE(n,init,pm,data,config,level,prio,api) \
 const struct behavior_driver_api *const bbp_registered_behavior_##n=api
#undef SYS_INIT
#define SYS_INIT(fn,level,priority) \
 int bbp_registered_init_##fn(void) { return fn(); } \
 const int bbp_registered_priority_##fn=priority
#undef ZMK_LISTENER
#define ZMK_LISTENER(name,callback) const void *const bbp_listener_##name=(const void*)&callback
#undef ZMK_SUBSCRIPTION
#define ZMK_SUBSCRIPTION(name,event) const char bbp_subscription_##name##_##event[]=#event
'''
HARNESS = r'''
#include "prefix.h"
#include "gamepad_runtime.h"
extern const struct behavior_driver_api *const bbp_registered_behavior_0;
extern int bbp_registered_init_gamepad_init(void);
extern const int bbp_registered_priority_gamepad_init;
extern const void *const bbp_listener_bbp_gamepad_context;
uint32_t bbp9981_usb_hid_connection_epoch(void) { return 1; }
int tp_tap_output_cancel(void) { return 0; }
int bbp_gamepad_usb_send(const struct bbp_gamepad_report_body *b,uint32_t u,uint32_t e) {
 (void)b;(void)u;(void)e;return -ENOTSUP;
}
int bbp_gamepad_ble_send(struct bt_conn *c,const struct bbp_gamepad_report_body *b,uint32_t e) {
 (void)c;(void)b;(void)e;return -ENOTSUP;
}
int main(void) {
 assert(bbp_registered_init_gamepad_init()==0);
 assert(bbp_registered_priority_gamepad_init==CONFIG_APPLICATION_INIT_PRIORITY);
 assert(bbp_listener_bbp_gamepad_context!=NULL);
 struct zmk_behavior_binding binding={.param1=UINT32_MAX};
 struct zmk_behavior_binding_event event={.position=42,.timestamp=0};
 assert(bbp_registered_behavior_0->binding_pressed(&binding,event)==-EINVAL);
 event.position=0;
 assert(bbp_registered_behavior_0->binding_pressed(&binding,event)==-EINVAL);
 binding.param1=0;
 assert(bbp_registered_behavior_0->binding_pressed(&binding,event)==ZMK_BEHAVIOR_OPAQUE);
 assert(bbp_registered_behavior_0->binding_released(&binding,event)==ZMK_BEHAVIOR_OPAQUE);
 assert(bbp_gamepad_context_epoch()==0&&bbp_gamepad_pointer_epoch()==0);
 return 0;
}
'''


def main():
    cc=os.environ.get('CC','cc'); build=ROOT/'.host-test-build'
    flags=['-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
           '-Wno-unused-function','-Wno-unused-variable','-fsanitize=undefined',
           '-fno-sanitize-recover=all']
    with tempfile.TemporaryDirectory(prefix='refactor-link-',dir=build) as directory:
        temp=Path(directory)
        (temp/'prefix.h').write_text(PREFIX);(temp/'main.c').write_text(HARNESS)
        includes=[temp,ROOT/'tests',build/'gamepad_runtime_include',ROOT/DRIVER]
        common=[cc,*flags,*['-I'+str(p) for p in includes],'-include',str(temp/'prefix.h')]
        for usb,ble in [(1,0),(0,1),(1,1)]:
            for tap in [0,1]:
                defs=[f'-DTEST_USB={usb}',f'-DTEST_BLE={ble}',f'-DTEST_TAP={tap}']
                objects=[]
                for source in [ROOT/DRIVER/'gamepad_runtime.c',ROOT/DRIVER/'behavior_gamepad.c',temp/'main.c']:
                    obj=temp/(source.stem+'.o')
                    subprocess.run([*common,*defs,'-c',str(source),'-o',str(obj)],check=True)
                    objects.append(obj)
                exe=temp/'linked'
                subprocess.run([cc,*flags,*map(str,objects),'-o',str(exe)],check=True)
                symbols=subprocess.check_output(['nm','--defined-only',str(exe)],text=True)
                for name in ['bbp_gamepad_key_event','bbp_registered_init_gamepad_init',
                             'bbp_registered_behavior_0','bbp_listener_bbp_gamepad_context']:
                    assert len(re.findall(r'\b'+name+r'$',symbols,re.M))==1,name
                subscriptions=re.findall(r'\bbbp_subscription_bbp_gamepad_context_\w+$',symbols,re.M)
                assert len(subscriptions)==2+usb+ble,(usb,ble,subscriptions)
                assert (' bbp_gamepad_connection\n' in symbols)==bool(ble)
                subprocess.run([str(exe)],check=True)
    print('PASS separate-TU gamepad link: USB-only/BLE-only/both x tap OFF/ON; single real adapter/init/listener, exact subscriptions and key API boundaries; host mocks only')


if __name__=='__main__':main()
