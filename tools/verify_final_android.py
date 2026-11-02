#!/usr/bin/env python3
"""Verify this final candidate's ARM ELF, source/SDK contracts and UF2; never flash."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import struct
import subprocess
from elf_hid_artifact import Image, require, check_pnp, check_uf2_matches
from final_android_hardware import check_standard

ROOT = Path(__file__).resolve().parents[1]
STOCK_SHA = '5c7e4106019e5baf9027c8a24e334c58f5e09c65cd3ff46b338c551c61da4ee3'
ANDROID_SHA = 'bb0373299db5f7b020fd2f7543a6ba3c29a0aaa8a6096678abb1cd6fcf7b03ac'
STOCK_UUIDS = [0x2800,0x2803,0x2a4a,0x2803,0x2a4b,0x2803,0x2a4d,0x2902,0x2908,
               0x2803,0x2a4d,0x2902,0x2908,0x2803,0x2a4d,0x2902,0x2908,
               0x2803,0x2a4d,0x2908,0x2803,0x2a4c]
GP_UUIDS = [0x2800,0x2803,0x2a4a,0x2803,0x2a4b,0x2803,0x2a4c,0x2803,0x2a4d,
            0x2902,0x2908,0x2803,0x2a4d,0x2908,0x2803,0x2a4e]


def check_elf(image):
    lo = image.address('_bt_gatt_service_static_list_start')
    hi = image.address('_bt_gatt_service_static_list_end')
    require(hi > lo and (hi-lo)%8 == 0, 'bad service iterable')
    services = []; hid = []; handle = 1
    for pos in range(lo, hi, 8):
        attrs, count = struct.unpack('<II', image.read(pos, 8))
        require(0 < count < 1024, 'bad attribute count')
        first = image.attr(attrs)
        require(first['uuid'] == 0x2800, 'unexpected non-primary service')
        uuid = image.uuid16(first['user'])
        services.append(dict(address=hex(pos), uuid16=uuid, attributes=count, first_handle=handle, last_handle=handle+count-1))
        if uuid == 0x1812:
            hid.append((pos, attrs, count, handle))
        handle += count
    require(len(hid) == 2, 'must register exactly two separate HIDS')
    require(hid[0][0] == image.address('hog_svc') and hid[0][2:] == (22,28), 'keyboard HOG layout/handles changed')
    require(hid[1][0] == image.address('zz_bbp_gamepad_svc') and hid[1][2:] == (16,54), 'independent gamepad layout/handles changed')
    require(hid[1][0] == hi-8, 'gamepad must be final registered static service')
    require(any(x['uuid16'] is None and x['first_handle']==50 and x['last_handle']==53 for x in services), 'Studio handles changed')
    stock = [image.attr(hid[0][1]+i*20) for i in range(22)]
    gp = [image.attr(hid[1][1]+i*20) for i in range(16)]
    require([a['uuid'] for a in stock] == STOCK_UUIDS, 'stock attributes differ')
    require([a['uuid'] for a in gp] == GP_UUIDS, 'gamepad attributes differ')
    def callback(address, name):
        require(address and address&~1 == image.address(name)&~1, 'wrong callback '+name)
    for i, name in {2:'read_hids_info',4:'read_hids_report_map',6:'read_hids_input_report',10:'read_hids_consumer_input_report',14:'read_hids_mouse_input_report'}.items():
        callback(stock[i]['read'],name)
        require(stock[i]['perm'] == (1 if i == 2 else 4), 'stock read permission changed')
    for i, name in [(18,'write_hids_leds_report'),(21,'write_ctrl_point')]:
        callback(stock[i]['write'],name)
    for i, ref in [(8,(1,1)),(12,(2,1)),(16,(3,1)),(19,(1,2))]:
        require(image.read(stock[i]['user'],2) == bytes(ref), 'stock reference changed')
    for i in (7,11,15):
        callback(stock[i]['read'],'bt_gatt_attr_read_ccc')
        callback(stock[i]['write'],'bt_gatt_attr_write_ccc')
        require(stock[i]['perm'] == 12, 'stock CCC permission changed')
    for i, properties in [(5,0x12),(9,0x12),(13,0x12),(17,0x0e),(20,0x04)]:
        require(image.read(stock[i]['user']+6,1) == bytes([properties]), 'stock characteristic properties changed')
    for i, name in {2:'bbp_gamepad_ble_read_info',4:'bbp_gamepad_ble_read_map',8:'bbp_gamepad_ble_read_report',12:'bbp_gamepad_ble_read_output',15:'bbp_gamepad_ble_read_protocol'}.items():
        callback(gp[i]['read'],name)
    for i, name in [(6,'bbp_gamepad_ble_write_cp'),(12,'bbp_gamepad_ble_write_output'),(15,'bbp_gamepad_ble_write_protocol')]:
        callback(gp[i]['write'],name)
    require(image.read(gp[10]['user'],2) == b'\x04\x01' and image.read(gp[13]['user'],2) == b'\x05\x02', 'gamepad refs wrong')
    for i in (2,4,8,10,13): require(gp[i]['perm'] == 4, 'gamepad read must be encrypted')
    for i in (9,12,15): require(gp[i]['perm'] == 12, 'gamepad CCC/output/protocol permissions wrong')
    require(gp[6]['perm'] == 8, 'gamepad CP permission wrong')
    for i, properties in [(7,0x12),(11,0x0e),(14,0x06)]:
        require(image.read(gp[i]['user']+6,1) == bytes([properties]), 'gamepad characteristic properties wrong')
    require(gp[9]['user'] not in [stock[i]['user'] for i in (7,11,15)], 'CCC storage overlaps stock')
    descriptors = image.symbols.get('zmk_hid_report_desc',[])
    require(bool(descriptors), 'missing stock map')
    for item in descriptors:
        require(item['st_size']==196 and hashlib.sha256(image.read(item['st_value'],196)).hexdigest()==STOCK_SHA, 'stock 196-byte Map changed')
    item = image.symbol('bbp_android_report_map')
    require(item['st_size']==184 and hashlib.sha256(image.read(item['st_value'],184)).hexdigest()==ANDROID_SHA, 'Android 184-byte Map changed')
    for name in ['bbp_gamepad_ble_send','zmk_hog_send_keyboard_report','zmk_hog_send_consumer_report','zmk_hog_send_mouse_report']:
        image.symbol(name)
    info=image.symbol('bbp_gamepad_ble_hid_info')
    require(image.read(info['st_value'],4)==bytes.fromhex('11010002'), 'false remote wake capability')
    cb=image.symbol('bt_conn_cb_bbp_gamepad_ble_conn_callbacks'); at=cb['st_value']; size=cb['st_size']
    start=image.address('_bt_conn_cb_list_start'); end=image.address('_bt_conn_cb_list_end')
    require(size>=8 and start<=at and at+size<=end and (at-start)%size==0, 'service lifecycle callbacks not registered')
    connected,disconnected=struct.unpack('<II',image.read(at,8))
    callback(connected,'bbp_gamepad_ble_connected_cb'); callback(disconnected,'bbp_gamepad_ble_disconnected_cb')
    return dict(services=services, stock_map_bytes=196, gamepad_map_bytes=184, stock_descriptor_copies=len(descriptors),
                input_id=4, input_body_bytes=10, output_id=5, output_body_bytes=4,
                stock_handles=[28,49], studio_handles=[50,53], gamepad_handles=[54,69],
                live_map_read_verified=False, live_notification_verified=False)


def check_commands(entries, stage):
    names=['hog.c','gamepad_runtime.c','gamepad_ble_service.c','gamepad_usb.c']
    found={}
    for entry in entries:
        tokens=entry.get('arguments') or shlex.split(entry['command'])
        text=' '.join(tokens)
        require(not any(x in text for x in ['pure_hog_guard','composite_ble_trial','composite_ble_candidate','gamepad_ble_extension.h','BBP9981_PURE_BLE','BBP9981_COMPOSITE_DEVICE']), 'forbidden composite/pure injection')
        name=Path(entry['file']).name
        if name in names:
            require(name not in found, 'duplicate compile TU '+name)
            source=Path(entry['file']); source=source if source.is_absolute() else Path(entry['directory'])/source
            suffix='zmk/app/src/hog.c' if name=='hog.c' else 'config/boards/arm/bbp9981/custom_driver/'+name
            require(source.resolve()==(stage/suffix).resolve(), 'wrong source path '+name)
            includes=[tokens[i+1] for i,t in enumerate(tokens[:-1]) if t=='-include']
            if name=='hog.c':
                require(len(includes)==1 and Path(includes[0]).name=='trackpad_hog_snapshot.h', 'stock HOG must retain ONLY baseline mouse snapshot prefix')
            found[name]=str(source)
    require(set(found)==set(names), 'missing actual production TU')
    return found


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('build','stage','out'): parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--commit',required=True); parser.add_argument('--run-id',type=int,required=True)
    args=parser.parse_args()
    require(len(args.commit)==40 and all(x in '0123456789abcdef' for x in args.commit) and args.run_id>0,'invalid provenance')
    names=subprocess.check_output(['git','ls-tree','-r','--name-only','-z',args.commit,'config'],cwd=ROOT).decode().split('\0')
    expected={n for n in names if n}; actual={p.relative_to(args.stage).as_posix() for p in (args.stage/'config').rglob('*') if p.is_file()}
    require(expected==actual,'staged config paths differ')
    for name in expected:
        before=subprocess.check_output(['git','show',args.commit+':'+name],cwd=ROOT)
        require((args.stage/name).read_bytes()==before,'staged config drift '+name)
    z=args.build/'zephyr'; config=(z/'.config').read_text()
    for item in ['CONFIG_BBP9981_GAMEPAD=y','CONFIG_ZMK_BLE=y','CONFIG_ZMK_USB=y','CONFIG_ZMK_POINTING=y','CONFIG_BT_ID_MAX=1','CONFIG_BT_GATT_CACHING=y','CONFIG_BT_GATT_SERVICE_CHANGED=y']:
        require(item in config.splitlines(),'configuration changed '+item)
    from elftools.elf.elffile import ELFFile
    with (z/'zmk.elf').open('rb') as stream:
        image=Image(ELFFile(stream))
        linked=check_elf(image); pnp=check_pnp(image,config,1)
        uf2=check_uf2_matches(image,(z/'zmk.uf2').read_bytes())
    result=dict(source_commit=args.commit,source_run=args.run_id,device_tested=False,
                bluetooth_keyboard_mouse_enabled=True,android_gamepad_acceptance='NOT DEVICE TESTED',
                registered=linked,pnp=pnp,uf2=uf2,
                compile_units=check_commands(json.loads((args.build/'compile_commands.json').read_text()),args.stage),
                hardware=check_standard(config,(z/'zephyr.dts').read_text()),source_files_verified=len(expected),
                limitation='DB hash changes with new tail; existing peer caches are not claimed unchanged. No pairing/settings erase or flashing performed.')
    args.out.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(result,ensure_ascii=False,indent=2))


if __name__=='__main__': main()
