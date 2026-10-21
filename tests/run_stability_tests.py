#!/usr/bin/env python3
"""Strict v2.3.1 RAM/protocol regression and simulated long-sequence invariants.
No clocks are slept, devices flashed, settings erased or pairings changed.
"""
from pathlib import Path
import os
import resource
import subprocess
import tempfile
from check_stability_scope import ROOT, SNAPSHOT, reference, verify_stability_scope

BUILD=ROOT/'.host-test-build'
CC=os.environ.get('CC','cc')
COMMON=['-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
        '-Wno-unused-function','-Wno-unused-variable','-fno-sanitize-recover=all']
USB_MODES=['normal','legacy','lifetime','protocol-wait','busy-ready','submitting',
           'submit-retry','reset-abort','endpoints','isr','errors','status','stock']


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))


def compile_case(source,output,includes,flags=(),sanitizer='undefined'):
    command=[CC,*COMMON,'-fsanitize='+sanitizer,*flags,*['-I'+str(p) for p in includes],
             str(ROOT/'tests'/source),'-o',str(output)]
    subprocess.run(command,cwd=ROOT,check=True)
    return output


def run(executable,*args,env=None,capture=False):
    return subprocess.run([str(executable),*args],cwd=ROOT,env=env,text=True,
                          capture_output=capture,preexec_fn=no_core,check=not capture)


def main():
    count=verify_stability_scope()
    print(f'PASS stability production scope: {count} baseline configs identical; one reviewed USB snapshot fix',flush=True)
    BUILD.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='stability-',dir=BUILD) as directory:
        temp=Path(directory)
        for name,include in [('input',BUILD/'include'),('microtap',BUILD/'include'),
                             ('gamepad',BUILD/'gamepad_runtime_include')]:
            exe=compile_case('test_stability_'+name+'.c',temp/name,[include])
            run(exe)
        for tp in [0,1]:
            exe=compile_case('test_stability_light_memory.c',temp/f'memory{tp}',[BUILD/'include'],
                             [f'-DSTABILITY_TP={tp}','-DTEST_TWO_LEDS=1'])
            run(exe)
        for boot in [0,1]:
            exe=compile_case('test_stability_usb_snapshot.c',temp/f'usb{boot}',
                             [ROOT/'tests/sdk_snapshot_mocks'],[f'-DCONFIG_ZMK_USB_BOOT={boot}'])
            for mode in USB_MODES:run(exe,mode)
            exe=compile_case('test_stability_usb_cycles.c',temp/f'usb-cycles{boot}',
                             [ROOT/'tests/sdk_snapshot_mocks'],[f'-DCONFIG_ZMK_USB_BOOT={boot}'])
            run(exe)
        legacy=temp/'legacy_snapshot.h';legacy.write_bytes(reference(SNAPSHOT))
        select_legacy='-DBBP_USB_SNAPSHOT_SOURCE="'+str(legacy)+'"'
        old=compile_case('test_sdk_usb.c',temp/'old-protocol',[ROOT/'tests/sdk_snapshot_mocks'],[select_legacy])
        result=run(old,'protocol-wait',capture=True)
        assert result.returncode!=0 and 'Assertion' in result.stderr, 'Old BOOT wait-window must fail at runtime, not compile time'
        print('PASS old/new protocol negative: identical public-API test builds, old runtime fails while fixed runtime passes',flush=True)
        env=os.environ.copy()
        # Non-PIE avoids intermittent host ASan shadow/startup mapping failures.
        env['ASAN_OPTIONS']='detect_stack_use_after_return=1:abort_on_error=0:exitcode=91:disable_coredump=1'
        asan_flags=['-fsanitize-address-use-after-scope','-no-pie']
        old=compile_case('test_sdk_usb.c',temp/'old-ram',[ROOT/'tests/sdk_snapshot_mocks'],
                         [*asan_flags,select_legacy],sanitizer='address')
        result=run(old,'lifetime',env=env,capture=True)
        assert result.returncode==91 and 'stack-use-after-return' in result.stderr,result.stderr[-4000:]
        fixed=compile_case('test_sdk_usb.c',temp/'fixed-ram',[ROOT/'tests/sdk_snapshot_mocks'],
                           asan_flags,sanitizer='address')
        for mode in ['lifetime','protocol-wait','normal']:run(fixed,mode,env=env)
        print('PASS old/new ASan negative: old stack source reports stack-use-after-return; private persistent source and protocol cases pass',flush=True)
    print('PASS stability suite: 26 USB configurations/cases, 30000 GP + 24000 input + 20000 microtap uninterrupted cycles, 4million memory/idle polls; simulated clocks only, NOT physical long-duration acceptance')


if __name__=='__main__':main()
