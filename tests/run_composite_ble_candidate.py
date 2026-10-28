#!/usr/bin/env python3
"""Build ONLY the opt-in composite BLE source candidate against host mocks.

The fixed HID mock natively contains 45 bytes, not the 196-byte report map. In
an auto-cleaned temporary directory this runner creates a copy with ONLY that
array replaced by the SHA-checked 196 bytes. The frozen fixture is never edited.
This is not a real Zephyr SDK build, firmware build, radio, device or OS test.
"""
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
from run_composite_hog_host import check_fixed, EXPECTED_REFS, ROOT
from run_composite_hog_snapshot_host import verify_snapshot
from run_composite_map_host import PURE_SHA, parse, validate, must_reject


def fixture_copy(directory, stock):
    original = (ROOT/'tests/gamepad_ble_mocks/zmk/hid.h').read_text()
    marker = 'static const uint8_t zmk_hid_report_desc[] = {'
    assert original.count(marker)==1
    start = original.index(marker)
    end = original.index('\n};',start)+3
    rows = ['    '+','.join(f'0x{x:02x}' for x in stock[i:i+12])+','
            for i in range(0,len(stock),12)]
    replacement = marker+'\n'+'\n'.join(rows)+'\n};'
    target = directory/'zmk/hid.h'
    target.parent.mkdir(parents=True)
    assert target.resolve().is_relative_to(directory.resolve())
    target.write_text(original[:start]+replacement+original[end:])
    assert len(stock)==196
    return target


def main():
    if not __debug__:
        raise RuntimeError('the oracle must not run under Python -O')
    stock=check_fixed(); verify_snapshot()
    cc=shlex.split(os.environ.get('CC','cc')); assert cc
    source=ROOT/'tests/test_composite_ble_candidate.c'
    fixture=ROOT/'tests/gamepad_ble_mocks'
    snapshot=ROOT/'config/boards/arm/bbp9981/custom_driver/trackpad_hog_snapshot.h'
    candidate=ROOT/'experiments/gamepad_protocol/composite_ble_candidate.h'
    old_extension=ROOT/'config/boards/arm/bbp9981/custom_driver/gamepad_ble_extension.h'
    common=[*cc,'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
            '-fno-omit-frame-pointer','-fno-pie','-no-pie']
    config=['-DCONFIG_ZMK_POINTING=1','-DCONFIG_ZMK_POINTING_SMOOTH_SCROLLING=0',
            '-DCONFIG_ZMK_HID_INDICATORS=1','-DCONFIG_ZMK_BLE_THREAD_STACK_SIZE=512',
            '-DCONFIG_ZMK_BLE_THREAD_PRIORITY=5','-DCONFIG_ZMK_BLE_INIT_PRIORITY=50',
            '-DCONFIG_ZMK_BLE_KEYBOARD_REPORT_QUEUE_SIZE=20',
            '-DCONFIG_ZMK_BLE_CONSUMER_REPORT_QUEUE_SIZE=5',
            '-DCONFIG_ZMK_BLE_MOUSE_REPORT_QUEUE_SIZE=20','-DCONFIG_ZMK_LOG_LEVEL=0',
            '-DCONFIG_A320_CURSOR_STEADY=1','-DCONFIG_A320_TAP_OUTPUT=1',
            '-DCONFIG_BBP9981_MENU_DIAG=1','-DCONFIG_ZMK_STUDIO=1']
    with tempfile.TemporaryDirectory(prefix='9981-ble-candidate-') as directory:
        temp=Path(directory)
        assert temp.resolve().parent==Path(tempfile.gettempdir()).resolve()
        assert temp.name.startswith('9981-ble-candidate-') and not temp.is_symlink()
        good=temp/'good'; bad=temp/'bad'
        fixture_copy(good,stock)
        altered=bytearray(stock); altered[195]^=1
        fixture_copy(bad,altered)
        prelude=temp/'peek-declaration.h'
        prelude.write_text('#include <zephyr/kernel.h>\nint k_msgq_peek(struct k_msgq *, void *);\n')
        includes=['-include',str(prelude),'-include',str(snapshot),'-include',str(candidate)]
        goodflags=['-I'+str(good),'-I'+str(fixture)]
        badflags=['-I'+str(bad),'-I'+str(fixture)]
        enabled='-DBBP9981_COMPOSITE_EXPERIMENT=1'
        for flag in (None,'-DBBP9981_COMPOSITE_EXPERIMENT=0'):
            result=subprocess.run([*common,*goodflags,*config,*([flag] if flag else []),
                                   *includes,'-c',str(source),'-o',str(temp/'gate.o')],
                                  stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            assert result.returncode!=0 and 'requires BBP9981_COMPOSITE_EXPERIMENT=1' in result.stderr
        # Original mock remains 45B and must never silently stand in for stock196.
        short=subprocess.run([*common,'-I'+str(fixture),*config,enabled,*includes,
                              '-c',str(source),'-o',str(temp/'short.o')],
                             stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        assert short.returncode!=0 and 'candidate needs pinned stock map' in short.stderr
        for key,value in [('CONFIG_ZMK_POINTING','0'),('CONFIG_ZMK_HID_INDICATORS','0'),
                          ('CONFIG_ZMK_POINTING_SMOOTH_SCROLLING','1')]:
            chosen=[flag for flag in config if not flag.startswith('-D'+key+'=')]
            result=subprocess.run([*common,*goodflags,*chosen,'-D'+key+'='+value,enabled,*includes,
                                   '-c',str(source),'-o',str(temp/'config.o')],
                                  stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            assert result.returncode!=0 and 'requires pointing/indicators ON and smooth scrolling OFF' in result.stderr
        # A production gamepad preinclude and this candidate must never stack.
        conflict=['-include',str(prelude),'-include',str(snapshot),'-include',str(old_extension),
                  '-include',str(candidate)]
        result=subprocess.run([*common,*goodflags,*config,enabled,*conflict,
                               '-c',str(source),'-o',str(temp/'conflict.o')],
                              stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        assert result.returncode!=0 and 'must replace, not accompany' in result.stderr
        print('PASS explicit enable/config gates, original 45B mock rejection, and old-extension conflict guard',flush=True)
        for sanitizer in ('undefined','address'):
            flags=['-fsanitize='+sanitizer]
            if sanitizer=='undefined': flags+=['-fno-sanitize-recover=all']
            exe=temp/('candidate-'+sanitizer)
            subprocess.run([*common,*goodflags,*config,enabled,*includes,*flags,str(source),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
            raw=subprocess.check_output([str(exe),'--map'])
            pure=subprocess.check_output([str(exe),'--pure'])
            refs=[tuple(map(int,x.split())) for x in subprocess.check_output([str(exe),'--refs'],text=True).splitlines()]
            assert refs==EXPECTED_REFS,refs
            assert len(pure)==182 and hashlib.sha256(pure).hexdigest()==PURE_SHA
            assert raw==stock+pure[:7]+b'\x04\x15\x00'+pure[8:] and len(raw)==380
            pairs=[(rid,typ) for rid,typ,_ in refs]
            validate(raw,stock,pure,pairs)
            collision=raw[:203]+b'\x03'+raw[204:]
            totals,_,_,owners=parse(collision)
            assert totals[3,1]==152 and len(owners[3,1])==2
            must_reject(collision,stock,pure,pairs,'mouse/gamepad input ID collision')
            no_min=raw[:204]+b'\x55\x00'+raw[206:]
            hat=next(f for f in parse(no_min)[1][4,1] if f['page']==1 and f['usage']==0x39 and not f['flags']&1)
            assert hat['lo']==-32768 and len(no_min)==380
            must_reject(no_min,stock,pure,pairs,'missing global logical minimum reset')
            must_reject(raw,stock,pure,pairs+[(4,1)],'duplicate reference')
            must_reject(raw,stock,pure,[(1,1),(2,1),(3,1),(1,2),(5,1),(4,1)],'wrong output type')
            altered_exe=temp/('bad-'+sanitizer)
            subprocess.run([*common,*badflags,*config,enabled,*includes,*flags,str(source),'-o',str(altered_exe)],check=True)
            subprocess.run([str(altered_exe),'--bad-map'],check=True)
            print('PASS '+sanitizer+' candidate build, full map/ref oracle and same-length wrong-stock negative',flush=True)
        assert check_fixed()==stock; verify_snapshot()
    print('LIMIT mock CCC/ATT/runtime/scheduling; pre-commit and post-gate windows remain, no SDK/ARM/OS/device proof')


if __name__=='__main__':
    main()
