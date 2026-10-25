#!/usr/bin/env python3
"""Explicit v2.3.3 authorization, layered on the unchanged v2.3.2 proof.

Only UPPER Z, persistent ordinary-mouse gain and MEDIA keyboard animation may
change. Every pre-existing C oracle/mock/SDK fixture remains byte-exact.
"""
from functools import lru_cache
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build_manual import bindings

BASE='11218ad83292f5ccf9105e41010c1062073e474d'
DRIVER='config/boards/arm/bbp9981/custom_driver/'
KEYMAPS={'config/bbp9981.keymap','config/boards/arm/bbp9981/bbp9981.keymap'}
CHANGED=KEYMAPS | {'config/bbp9981.conf','config/boards/arm/bbp9981/Kconfig.board',
                   'config/boards/arm/bbp9981/CMakeLists.txt',DRIVER+'a320_0x57.c',
                   DRIVER+'trackpad_pointer.h',DRIVER+'keyboard_backlight.c'}
ADDED={DRIVER+'trackpad_cursor_speed.h',DRIVER+'trackpad_cursor_speed.c',
       DRIVER+'behavior_cursor_speed.c','config/dts/bindings/behaviors/zmk,behavior-cursor-speed.yaml'}

# v2.3.3.2 authorizes exactly one further edit: resolve the MEDIA layer by
# stable ID instead of its order index. It is projected out before the older
# proofs run, so those proofs keep comparing byte-exact historical trees.
REPAIR=DRIVER+'gamepad_runtime.c'
REPAIR_OLD=b'    return zmk_keymap_highest_layer_active() == BBP_GAMEPAD_LAYER;'
REPAIR_NEW=(b'    /* The SDK returns an order index; Studio can reorder stable layer IDs. */\n'
            b'    return zmk_keymap_layer_index_to_id(zmk_keymap_highest_layer_active()) == BBP_GAMEPAD_LAYER;')


def project(path,data):
    """Reverse the single layer-identity fix; every other byte stays as-is."""
    if path!=REPAIR or data.count(REPAIR_NEW)!=1:
        return data
    return data.replace(REPAIR_NEW,REPAIR_OLD,1)
CONF_ADDITION='''
# v2.3.3: UPPER Z gain memory; only MEDIA keyboard backlight changes pattern.
CONFIG_BBP9981_CURSOR_SPEED=y
CONFIG_BBP9981_MEDIA_BREATHE=y
'''
CMAKE_ADDITION='''    # v2.3.3: DTS precedes Kconfig; always provide the node's OFF-safe adapter.
    zephyr_library_sources(custom_driver/behavior_cursor_speed.c)
    if(CONFIG_BBP9981_CURSOR_SPEED)
        zephyr_library_sources(custom_driver/trackpad_cursor_speed.c)
    endif()
'''
KEYMAP_NODE='''        cursor_speed: cursor_speed {
            compatible = "zmk,behavior-cursor-speed";
            #binding-cells = <0>;
            display-name = "Mouse gain: cycle five persistent presets";
        };
'''
KEYMAP_ROW_OLD='    &kp LSHFT    &none      &rgb_ug RGB_BRD &bl BL_DEC        &rgb_ug RGB_TOG  &bl BL_INC       &rgb_ug RGB_BRI &aa_scroll_gesture_key &td_boot_out     &kp ENTER\n'
KEYMAP_ROW_NEW='    &kp LSHFT    &cursor_speed &rgb_ug RGB_BRD &bl BL_DEC        &rgb_ug RGB_TOG  &bl BL_INC       &rgb_ug RGB_BRI &aa_scroll_gesture_key &td_boot_out     &kp ENTER\n'
KCONFIG_ADDITION='''config BBP9981_CURSOR_SPEED
    bool "Five persistent ordinary mouse gain presets"
    depends on BOARD_BBP9981 && ZMK_POINTING && SETTINGS
    default n
    help
      UPPER Z cycles 0.6/0.9/1.2/1.5/1.8 gain; default 1.2 and Ctrl half.
      Save the last preset using an independent, debounced settings key.
      Scroll/gamepad keep their original gain; no sensor DPI register writes.

config BBP9981_MEDIA_BREATHE
    bool "Slow MEDIA keyboard backlight breathing"
    depends on BOARD_BBP9981 && ZMK_RGB_UNDERGLOW && LED_PWM
    default n
    help
      Only MEDIA changes from flashing to a 3.6 second triangular breath.
      Keep the user's brightness/off state, idle and other layer patterns.

'''


@lru_cache(maxsize=None)
def base(path):
    return subprocess.check_output(['git','show',BASE+':'+path],cwd=ROOT)


def strip_blocks(data,flag):
    """Disable only this named feature, retaining an explicit original else."""
    out,depth,take,blocks=[],0,False,0
    for line in data.decode().splitlines(keepends=True):
        directive=line.strip()
        if not depth and directive=='#ifdef '+flag:
            depth,take=1,False
            blocks+=1
        elif depth:
            if directive.startswith(('#if ','#ifdef ','#ifndef ')):
                depth+=1
                if take: out.append(line)
            elif directive.startswith('#endif'):
                depth-=1
                if depth and take: out.append(line)
            elif directive=='#else' and depth==1:
                take=True
            elif directive.startswith('#elif') and depth==1:
                raise AssertionError('No unreviewed feature elif')
            elif take:
                out.append(line)
        else:
            out.append(line)
    assert not depth and blocks,('Missing/unbalanced feature',flag)
    return ''.join(out).encode()


def remove_exact(path,data,text):
    newline='\r\n' if b'\r\n' in base(path) else '\n'
    block=text.replace('\n',newline).encode()
    assert data.count(block)==1,('Exact feature addition missing',path)
    return data.replace(block,b'')


def restore(path,data):
    if path in KEYMAPS:
        # This is an explicit historical projection, NOT real Kconfig-OFF DTS.
        # Real OFF keeps the node and uses the compiled opaque no-op adapter.
        data=remove_exact(path,data,KEYMAP_NODE)
        newline='\r\n' if b'\r\n' in base(path) else '\n'
        before=KEYMAP_ROW_NEW.replace('\n',newline).encode()
        after=KEYMAP_ROW_OLD.replace('\n',newline).encode()
        assert data.count(before)==1,('Exact Z-only row missing',path)
        return data.replace(before,after)
    if path in {DRIVER+'a320_0x57.c',DRIVER+'trackpad_pointer.h'}:
        return strip_blocks(data,'CONFIG_BBP9981_CURSOR_SPEED')
    if path==DRIVER+'keyboard_backlight.c':
        return strip_blocks(data,'CONFIG_BBP9981_MEDIA_BREATHE')
    if path=='config/bbp9981.conf': return remove_exact(path,data,CONF_ADDITION)
    if path=='config/boards/arm/bbp9981/Kconfig.board': return remove_exact(path,data,KCONFIG_ADDITION)
    if path=='config/boards/arm/bbp9981/CMakeLists.txt': return remove_exact(path,data,CMAKE_ADDITION)
    return data


@lru_cache(maxsize=1)
def verified_v232_view():
    paths=subprocess.check_output(['git','ls-tree','-r','--name-only',BASE,'config'],cwd=ROOT,text=True).splitlines()
    actual={str(p.relative_to(ROOT)) for p in (ROOT/'config').rglob('*') if p.is_file()}
    assert actual==set(paths)|ADDED,('Unapproved config files',actual^(set(paths)|ADDED))
    assert project(REPAIR,(ROOT/REPAIR).read_bytes())!= (ROOT/REPAIR).read_bytes(), \
        'Authorized layer-identity fix is missing'
    changes={p for p in paths if project(p,(ROOT/p).read_bytes())!=base(p)}
    assert changes==CHANGED,('Unapproved production scope',changes^CHANGED)
    view={p:restore(p,project(p,(ROOT/p).read_bytes())) for p in paths}
    for p in paths: assert view[p]==base(p),'Changed outside explicit v2.3.3 additions: '+p
    # Check ACTUAL enabled maps, not merely the restored historical view.
    mapped=[]
    for p in KEYMAPS:
        current=bindings((ROOT/p).read_text());previous=bindings(base(p).decode())
        assert previous['upper'][28]=='&none'
        previous['upper'][28]='&cursor_speed'
        assert current==previous,('Non-Z binding changed',p)
        mapped.append(current)
    assert mapped[0]==mapped[1]
    tests=subprocess.check_output(['git','ls-tree','-r','--name-only',BASE,'tests'],cwd=ROOT,text=True).splitlines()
    for p in tests:
        if Path(p).suffix in {'.c','.h'}:
            assert (ROOT/p).read_bytes()==base(p),'Frozen pre-existing C oracle/fixture changed: '+p
    return view


def main():
    view=verified_v232_view()
    print(f'PASS v2.3.3 scope: {len(view)-len(CHANGED)}/{len(view)} existing configs byte-exact, '
          '8 explicitly scoped edits + 4 new speed files; complete v2.3.2 historical projection restored; '
          'enabled 4x42 maps differ ONLY at UPPER Z; all old C oracles/SDK fixtures frozen')


if __name__=='__main__':main()
