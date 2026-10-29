#!/usr/bin/env python3
"""Real trial adapter/HOG fixture + separate lifecycle TU, no device claims."""
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
from run_composite_ble_candidate import fixture_copy
from run_composite_hog_host import check_fixed, EXPECTED_REFS, ROOT
from run_composite_hog_snapshot_host import verify_snapshot
from run_composite_map_host import validate, PURE_SHA

def main():
    stock=check_fixed(); verify_snapshot()
    cc=shlex.split(os.environ.get('CC','cc'))
    fixture=ROOT/'tests/gamepad_ble_mocks'
    snapshot=ROOT/'config/boards/arm/bbp9981/custom_driver/trackpad_hog_snapshot.h'
    candidate=ROOT/'experiments/gamepad_protocol/composite_ble_trial.h'
    lifecycle=ROOT/'experiments/gamepad_protocol/composite_trial_lifecycle.c'
    common=[*cc,'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
            '-fno-omit-frame-pointer','-fno-pie','-no-pie']
    config=['-DCONFIG_ZMK_POINTING=1','-DCONFIG_ZMK_POINTING_SMOOTH_SCROLLING=0',
            '-DCONFIG_ZMK_HID_INDICATORS=1','-DCONFIG_ZMK_BLE_THREAD_STACK_SIZE=512',
            '-DCONFIG_ZMK_BLE_THREAD_PRIORITY=5','-DCONFIG_ZMK_BLE_INIT_PRIORITY=50',
            '-DCONFIG_ZMK_BLE_KEYBOARD_REPORT_QUEUE_SIZE=20',
            '-DCONFIG_ZMK_BLE_CONSUMER_REPORT_QUEUE_SIZE=5',
            '-DCONFIG_ZMK_BLE_MOUSE_REPORT_QUEUE_SIZE=20','-DCONFIG_ZMK_LOG_LEVEL=0',
            '-DCONFIG_A320_CURSOR_STEADY=1','-DCONFIG_A320_TAP_OUTPUT=1',
            '-DCONFIG_BBP9981_MENU_DIAG=1','-DCONFIG_ZMK_STUDIO=1',
            '-DCONFIG_BT_MAX_CONN=5']
    with tempfile.TemporaryDirectory(prefix='9981-device-trial-') as directory:
        temp=Path(directory); good=temp/'good'; fixture_copy(good,stock)
        prelude=temp/'trial-api.h'
        prelude.write_text('''#pragma once
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/conn.h>
int k_msgq_peek(struct k_msgq *, void *);
uint8_t bt_conn_index(const struct bt_conn *);
struct bt_conn_cb {
 void (*connected)(struct bt_conn *,uint8_t);
 void (*disconnected)(struct bt_conn *,uint8_t);
};
#define BT_CONN_CB_DEFINE(name) struct bt_conn_cb name
''')
        includes=['-I'+str(good),'-I'+str(fixture),'-I'+str(ROOT/'tests'),'-include',str(prelude)]
        hogflags=[*includes,'-include',str(snapshot),'-include',str(candidate)]
        # All old config/fixture gates remain. The new device opt-in cannot be omitted.
        disabled=subprocess.run([*common,*hogflags,*config,'-c',str(ROOT/'tests/test_composite_ble_trial.c'),
                                 '-o',str(temp/'disabled.o')],capture_output=True,text=True)
        assert disabled.returncode and 'requires BBP9981_COMPOSITE_DEVICE_TRIAL=1' in disabled.stderr
        for sanitizer in ('undefined','address'):
            flags=['-fsanitize='+sanitizer]
            if sanitizer=='undefined': flags+=['-fno-sanitize-recover=all']
            obj=temp/('lifecycle-'+sanitizer+'.o')
            subprocess.run([*common,*includes,*config,*flags,
                            '-DBBP9981_COMPOSITE_DEVICE_TRIAL_LIFECYCLE=1',
                            '-c',str(lifecycle),'-o',str(obj)],check=True)
            exe=temp/('trial-'+sanitizer)
            subprocess.run([*common,*hogflags,*config,*flags,'-DBBP9981_COMPOSITE_DEVICE_TRIAL=1',
                            str(ROOT/'tests/test_composite_ble_trial.c'),str(obj),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
            raw=subprocess.check_output([str(exe),'--map'])
            pure=subprocess.check_output([str(exe),'--pure'])
            refs=[tuple(map(int,x.split())) for x in subprocess.check_output([str(exe),'--refs'],text=True).splitlines()]
            assert refs==EXPECTED_REFS
            assert len(pure)==182 and hashlib.sha256(pure).hexdigest()==PURE_SHA
            assert raw==stock+pure[:7]+b'\x04\x15\x00'+pure[8:] and len(raw)==380
            validate(raw,stock,pure,[(a,b) for a,b,_ in refs])
            symbols=subprocess.check_output(['nm',str(exe)],text=True)
            assert sum(line.rstrip().endswith(' bbp_trial_connection_reset') for line in symbols.splitlines())==1
            assert sum(line.rstrip().endswith(' bbp_trial_gate') for line in symbols.splitlines())==1
            print('PASS trial '+sanitizer+' descriptor, standard CCC, application claim and separately linked lifecycle',flush=True)
    assert check_fixed()==stock;verify_snapshot()
    print('LIMIT no real Zephyr CCC, encryption, radio, GCController, Moonlight or ARM evidence; single claimed call may be in flight')

if __name__=='__main__':main()
