#!/usr/bin/env python3
"""v2.3.3 actual-ON integration plus separately frozen feature-OFF proofs.

Host callbacks/mocked flash are not a physical restart, visual or HID test.
"""
from pathlib import Path
import os
import re
import resource
import subprocess
import tempfile
from user_preferences_scope import (ROOT, DRIVER, KEYMAPS, base, restore,
                                    verified_v232_view, select_cursor_speed_keymap)

CC=os.environ.get('CC','cc')
COMMON=['-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
        '-Wno-unused-function','-Wno-unused-variable','-fno-sanitize-recover=all']


def no_core():resource.setrlimit(resource.RLIMIT_CORE,(0,0))


def build_run(source,target,includes,flags=(),asan=False,negative=False):
    sanitizer=['-fsanitize=address,undefined','-no-pie','-fno-omit-frame-pointer'] if asan else ['-fsanitize=undefined']
    subprocess.run([CC,*COMMON,*sanitizer,*flags,*['-I'+str(p) for p in includes],
                    str(ROOT/'tests'/source),'-o',str(target)],cwd=ROOT,check=True)
    env=os.environ.copy()
    if asan:env['ASAN_OPTIONS']='detect_stack_use_after_return=1:abort_on_error=0:exitcode=91:disable_coredump=1'
    result=subprocess.run([str(target)],cwd=ROOT,env=env,preexec_fn=no_core,
                          capture_output=negative,text=True,check=not negative)
    if negative:
        assert result.returncode!=0 and 'Assertion' in result.stderr,result.stderr
        print('PASS live negative: actual A320 interleaving oracle rejects live-gain bypass of frame latch',flush=True)


def source_contracts():
    view=verified_v232_view()
    conf=(ROOT/'config/bbp9981.conf').read_text()
    for flag in ['CONFIG_BBP9981_CURSOR_SPEED=y','CONFIG_BBP9981_MEDIA_BREATHE=y',
                 'CONFIG_SETTINGS=y','CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE=1000']:
        assert re.search('^'+re.escape(flag)+'$',conf,re.M),flag
    schema=(ROOT/'config/dts/bindings/behaviors/zmk,behavior-cursor-speed.yaml').read_text()
    assert 'include: zero_param.yaml' in schema and not re.search(r'^binding-cells:',schema,re.M)
    # Resolve actual feature islands with the system CPP as well as the export
    # parser; fail if either ON or OFF metadata/binding escapes its gate.
    for path in KEYMAPS:
        data=(ROOT/path).read_text()
        islands=re.findall(r'^#ifdef CONFIG_BBP9981_CURSOR_SPEED\n.*?^#endif\n',data,re.M|re.S)
        assert len(islands)==2
        fragment=''.join(islands)
        for enabled in [False,True]:
            flags=['-DCONFIG_BBP9981_CURSOR_SPEED=1'] if enabled else []
            expanded=subprocess.check_output([CC,'-E','-P','-x','assembler-with-cpp',*flags,'-'],input=fragment,text=True)
            expected=select_cursor_speed_keymap(fragment,enabled)
            assert expanded.split()==expected.split(),(path,enabled,expanded,expected)
        assert select_cursor_speed_keymap(data,False).encode()==base(path).replace(b'\r\n',b'\n')
    # These changes were NOT authorized: the feature projection must reject
    # altered GPIO, fixed denominator, SYM flash rate and non-Z key bindings.
    for path,before,after in [
        (DRIVER+'a320_0x57.c',b'MOTION_GPIO_PIN 2',b'MOTION_GPIO_PIN 3'),
        (DRIVER+'trackpad_pointer.h',b'TP_POINTER_DENOMINATOR 400',b'TP_POINTER_DENOMINATOR 401'),
        (DRIVER+'keyboard_backlight.c',b'BLINK_INTERVAL_MS 500',b'BLINK_INTERVAL_MS 501'),
        ('config/bbp9981.keymap',b'&kp LSHFT',b'&kp LALT'),
    ]:
        current=(ROOT/path).read_bytes();changed=current.replace(before,after,1)
        assert changed!=current and restore(path,changed)!=base(path),path
    print(f'PASS v2.3.3 source contracts: {len(view)} exact v2.3.2 files restored, 4x42 current maps only UPPER Z; CPP ON/OFF, schema, persistence defaults, out-of-scope counterexamples',flush=True)


def main():
    source_contracts()
    build=ROOT/'.host-test-build';build.mkdir(exist_ok=True)
    stubs=build/'include'
    assert (stubs/'zephyr/kernel.h').is_file(),'Run run_host_tests.py to create ordinary host stubs'
    with tempfile.TemporaryDirectory(prefix='user-preferences-',dir=build) as directory:
        temp=Path(directory)
        for settings in [0,1]:
            for metadata in [0,1]:
                build_run('test_cursor_speed.c',temp/f'service{settings}{metadata}',
                          [ROOT/'tests/cursor_speed_mocks'],
                          [f'-DCONFIG_SETTINGS={settings}',f'-DCONFIG_ZMK_BEHAVIOR_METADATA={metadata}','-pedantic'])
        build_run('test_cursor_presets_math.c',temp/'math',[stubs])
        for micro in [0,1]:
            build_run('test_cursor_presets_driver.c',temp/f'driver{micro}',[stubs],
                      [f'-DTEST_CURSOR_MICRO={micro}'])
        media=['-DTEST_TWO_LEDS=1','-DCONFIG_SETTINGS=1']
        build_run('test_media_breathe.c',temp/'media',[stubs],[*media,'-DCONFIG_BBP9981_MEDIA_BREATHE=1'])
        for label,extra in [('led',[]),('pwm',['-DTEST_MEDIA_OFF_LOW_PWM=1']),
                            ('memory',['-DTEST_MEDIA_OFF_LIGHT_MEMORY=1'])]:
            build_run('test_media_breathe.c',temp/('media-off-'+label),[stubs],[*media,*extra])
        build_run('test_cursor_speed.c',temp/'service-asan',[ROOT/'tests/cursor_speed_mocks'],
                  ['-DCONFIG_SETTINGS=1','-DCONFIG_ZMK_BEHAVIOR_METADATA=1','-pedantic'],asan=True)
        build_run('test_cursor_presets_math.c',temp/'math-asan',[stubs],asan=True)
        build_run('test_cursor_presets_driver.c',temp/'driver-asan',[stubs],asan=True)
        build_run('test_media_breathe.c',temp/'media-asan',[stubs],
                  [*media,'-DCONFIG_BBP9981_MEDIA_BREATHE=1'],asan=True)
        build_run('test_cursor_presets_driver.c',temp/'negative',[stubs],
                  ['-DTEST_CURSOR_MICRO=1','-DTEST_NEGATIVE_LIVE_GAIN=1'],negative=True)
    print('PASS user preferences: actual ON service/behavior/pointer/A320/shadow/filter and MEDIA, true feature OFF, strict UBSan + ASan; no physical flash, visual, radio or host-speed acceptance')


if __name__=='__main__':main()
