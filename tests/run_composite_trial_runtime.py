#!/usr/bin/env python3
"""Trial CCC gate with unchanged production runtime and independent TU link."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True
from run_composite_candidate_runtime import (ROOT,check_runtime,make_runtime_shims,separate_tu_test)
from run_composite_ble_candidate import fixture_copy
from run_composite_hog_host import check_fixed
from run_composite_hog_snapshot_host import verify_snapshot

def main():
    stock=check_fixed();verify_snapshot();check_runtime()
    cc=shlex.split(os.environ.get('CC','cc'))
    candidate=ROOT/'experiments/gamepad_protocol/composite_ble_trial.h'
    snapshot=ROOT/'config/boards/arm/bbp9981/custom_driver/trackpad_hog_snapshot.h'
    mocks=ROOT/'tests/gamepad_ble_mocks'
    common=[*cc,'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
            '-Wno-unused-function','-fno-omit-frame-pointer','-fno-pie','-no-pie']
    config=['-DBBP9981_COMPOSITE_DEVICE_TRIAL=1','-DCONFIG_BT_MAX_CONN=5','-DCONFIG_ZMK_POINTING=1',
            '-DCONFIG_ZMK_POINTING_SMOOTH_SCROLLING=0','-DCONFIG_ZMK_HID_INDICATORS=1',
            '-DCONFIG_ZMK_BLE_THREAD_STACK_SIZE=512','-DCONFIG_ZMK_BLE_THREAD_PRIORITY=5',
            '-DCONFIG_ZMK_BLE_INIT_PRIORITY=50','-DCONFIG_ZMK_BLE_KEYBOARD_REPORT_QUEUE_SIZE=20',
            '-DCONFIG_ZMK_BLE_CONSUMER_REPORT_QUEUE_SIZE=5','-DCONFIG_ZMK_BLE_MOUSE_REPORT_QUEUE_SIZE=20',
            '-DCONFIG_ZMK_LOG_LEVEL=0','-DCONFIG_A320_CURSOR_STEADY=1','-DCONFIG_A320_TAP_OUTPUT=1',
            '-DCONFIG_BBP9981_MENU_DIAG=1','-DCONFIG_ZMK_STUDIO=1','-DCONFIG_ZMK_USB=1',
            '-DCONFIG_ZMK_BLE=1','-DCONFIG_BT_GATT_ENFORCE_SUBSCRIPTION=1',
            '-DCONFIG_APPLICATION_INIT_PRIORITY=50']
    with tempfile.TemporaryDirectory(prefix='9981-trial-real-runtime-') as directory:
        temp=Path(directory);overlay=temp/'include';fixture_copy(overlay,stock);make_runtime_shims(overlay)
        conn=overlay/'zephyr/bluetooth/conn.h';conn.parent.mkdir(parents=True,exist_ok=True)
        conn.write_text((mocks/'zephyr/bluetooth/conn.h').read_text()+
                        '\nstatic inline uint8_t bt_conn_index(const struct bt_conn *c){return c->dst.a.val[5];}\n')
        fixture_text=(ROOT/'tests/test_composite_candidate_runtime.c').read_text()
        assert fixture_text.count('int main(void) {')==1
        fixture_text=fixture_text.replace('int main(void) {','int historical_runtime_main(void) {',1)
        fixture_text=fixture_text.replace('#include "../config/boards/arm/bbp9981/custom_driver/gamepad_runtime.c"',
                                         '#include "'+str(ROOT/'config/boards/arm/bbp9981/custom_driver/gamepad_runtime.c')+'"')
        (overlay/'trial_runtime_fixture.inc').write_text(fixture_text)
        prelude=temp/'peek.h';prelude.write_text('#include <zephyr/kernel.h>\nint k_msgq_peek(struct k_msgq *, void *);\n')
        includes=['-I'+str(overlay),'-I'+str(mocks),'-I'+str(ROOT/'tests'),'-include',str(prelude),
                  '-include',str(snapshot),'-include',str(candidate)]
        for sanitizer in ('undefined','address'):
            san=['-fsanitize='+sanitizer]
            if sanitizer=='undefined':san+=['-fno-sanitize-recover=all']
            exe=temp/('runtime-'+sanitizer)
            subprocess.run([*common,*config,*includes,*san,str(ROOT/'tests/test_composite_trial_runtime.c'),
                            '-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
            separate_tu_test(temp,overlay,mocks,common,config,prelude,snapshot,candidate,sanitizer)
    assert check_fixed()==stock;verify_snapshot();check_runtime()
    print('PASS trial linked unchanged runtime source; no actual Zephyr CCC, ARM, radio or OS proof')

if __name__=='__main__':main()
