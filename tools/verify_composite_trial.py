#!/usr/bin/env python3
"""Fail-closed ARM ELF/UF2 gate for the opt-in composite trial; never flash.

The 380-byte map is virtual (stock + pure + ID/min patch), not a contiguous
ELF object. This gate checks its compiled inputs and registered callbacks;
it does not execute ATT reads or prove live notification delivery.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shlex
import struct
from verify_pure_candidate import Image, require, check_pnp, check_uf2_matches
from stage_composite_trial import BOARD, PURE_SHA, STOCK_SHA

UUIDS = [0x2800,0x2803,0x2a4a,0x2803,0x2a4b,
         0x2803,0x2a4d,0x2902,0x2908,0x2803,0x2a4d,0x2902,0x2908,
         0x2803,0x2a4d,0x2902,0x2908,0x2803,0x2a4d,0x2908,0x2803,0x2a4c,
         0x2803,0x2a4d,0x2908,0x2803,0x2a4d,0x2902,0x2908]
CALLBACK = 'bt_conn_cb_bbp_composite_trial_connection'
BASELINE = Path(__file__).resolve().parents[1]/'tests/fixtures/composite_trial_standard'
BASELINE_CONFIG_SHA = '51e29321a412aaa8ce1d27e108fd2fdd030daa2e61cd6ffe120bfeb0190f0ce4'
BASELINE_DTS_SHA = '77d47ac09693cfafe1f9c69717e5cd520341cbea59d5daf015eaac82cf007f99'
# No Kconfig changes are expected: the trial uses compile definitions only.
EXPECTED_CONFIG_CHANGES = {}


def assigned_config(text):
    pairs=re.findall(r'^(CONFIG_\w+)=(.*)$',text,re.M)
    require(len(pairs)==len(dict(pairs)),'duplicate assigned Kconfig value')
    return dict(pairs)


def normalize_dts(text):
    # Actual Zephyr dts output, not compiler diagnostics or arbitrary fragments.
    require(text.count('/dts-v1/;')==1 and text.rstrip().endswith('};'),'invalid complete DTS')
    text=text[text.index('/dts-v1/;'):].replace('\r\n','\n')
    # Generated numeric assignments may renumber; all node properties, references,
    # bindings, quoted strings and hardware fields remain byte-exact.
    return re.sub(r'(?m)^[ \t]*phandle = < 0x[0-9a-f]+ >;\n','',text).strip()+'\n'


def check_standard(config,dts):
    raw=(BASELINE/'assigned_config.json').read_bytes();raw_dts=(BASELINE/'zephyr.dts').read_bytes()
    require(hashlib.sha256(raw).hexdigest()==BASELINE_CONFIG_SHA,'standard config fixture changed')
    require(hashlib.sha256(raw_dts).hexdigest()==BASELINE_DTS_SHA,'standard DTS fixture changed')
    meta=json.loads((BASELINE/'provenance.json').read_text())
    require(meta['run_id']==39743108339 and meta['build_commit']=='e2c4966136b5e6af5db795c6c46048672758ba30','wrong standard provenance')
    before=json.loads(raw);after=assigned_config(config)
    require(len(before)==1170,'incomplete standard config baseline')
    changes={key:{'before':before.get(key),'after':after.get(key)} for key in sorted(set(before)|set(after)) if before.get(key)!=after.get(key)}
    require(changes==EXPECTED_CONFIG_CHANGES,'unexpected complete Kconfig diff: '+json.dumps(changes,sort_keys=True))
    require(normalize_dts(dts)==normalize_dts(raw_dts.decode()),'standard DTS/hardware/keymap differs outside generated phandle assignments')
    return dict(run_id=39743108339,build_commit=meta['build_commit'],assigned_values=len(after),
                expected_config_changes=EXPECTED_CONFIG_CHANGES,complete_normalized_dts_identical=True,
                config_fixture_sha256=BASELINE_CONFIG_SHA,dts_fixture_sha256=BASELINE_DTS_SHA)


def check_elf(image, max_connections):
    require(1 <= max_connections <= 255, 'unsupported connection layout')
    start=image.address('_bt_gatt_service_static_list_start')
    end=image.address('_bt_gatt_service_static_list_end')
    require(end>start and (end-start)%8==0, 'invalid service iterable')
    hid=[]
    for pos in range(start,end,8):
        attrs,count=struct.unpack('<II',image.read(pos,8))
        require(0<count<1024,'invalid service count')
        first=image.attr(attrs)
        require(first['uuid']==0x2800,'non-primary service')
        if image.uuid16(first['user'])==0x1812:
            hid.append((pos,attrs,count))
    require(len(hid)==1,'expected exactly one HID service')
    pos,attrs,count=hid[0]
    require(pos==image.address('hog_svc') and attrs==image.address('attr_hog_svc') and count==29,
            'wrong HOG table or count')
    fields=[image.attr(attrs+i*20) for i in range(count)]
    require([a['uuid'] for a in fields]==UUIDS,'wrong 29-attribute order')
    def pointer(actual, name):
        require(actual!=0 and actual&~1==image.address(name)&~1,'wrong callback: '+name)
    for index,ref in [(8,(1,1)),(12,(2,1)),(16,(3,1)),(19,(1,2)),(24,(5,2)),(28,(4,1))]:
        require(image.read(fields[index]['user'],2)==bytes(ref),'wrong report reference/value pairing')
        require(fields[index]['perm']&4,'unencrypted report reference')
    reads={2:'read_hids_info',4:'read_hids_report_map',6:'read_hids_input_report',
           10:'read_hids_consumer_input_report',14:'read_hids_mouse_input_report',
           23:'bbp_candidate_read_output',26:'bbp_candidate_read_input',
           24:'bbp_candidate_read_ref',28:'bbp_candidate_read_ref'}
    for index,name in reads.items(): pointer(fields[index]['read'],name)
    for index,name in [(18,'write_hids_leds_report'),(21,'write_ctrl_point'),(23,'bbp_candidate_write_output')]:
        pointer(fields[index]['write'],name)
    for index in [7,11,15,27]:
        pointer(fields[index]['read'],'bt_gatt_attr_read_ccc')
        pointer(fields[index]['write'],'bt_gatt_attr_write_ccc')
        require(fields[index]['perm']&12==12,'unencrypted CCC')
    for index in [6,10,14,23,26]:
        require(fields[index]['perm']&4,'unencrypted report')
    require(fields[23]['perm']&12==12,'output must be encrypted read/write')
    for index,properties in [(5,0x12),(9,0x12),(13,0x12),(17,0x0e),(22,0x0e),(25,0x12)]:
        require(image.read(fields[index]['user']+6,1)==bytes([properties]),'wrong characteristic properties')
    # Pinned _bt_gatt_ccc: cfg[BT_GATT_CCC_MAX] then u16 value, padding,
    # cfg_changed, cfg_write, cfg_match. cfg element ARM size is 10 bytes;
    # this layout MUST be checked against the actual pinned SDK/ELF.
    callbacks_offset=(max_connections*10+2+3)&~3
    changed,write,match=struct.unpack('<III',image.read(fields[27]['user']+callbacks_offset,12))
    pointer(changed,'bbp_candidate_ccc_changed');pointer(write,'bbp_candidate_ccc_write')
    require(match==0,'unexpected cfg_match')
    stock=image.symbol('zmk_hid_report_desc');pure=image.symbol('bbp_exp_report_map')
    require(stock['st_size']==196 and pure['st_size']==182,'wrong compiled descriptor input sizes')
    stock_bytes=image.read(stock['st_value'],196);pure_bytes=image.read(pure['st_value'],182)
    require(hashlib.sha256(stock_bytes).hexdigest()==STOCK_SHA,'stock descriptor changed')
    require(hashlib.sha256(pure_bytes).hexdigest()==PURE_SHA,'pure descriptor changed')
    virtual=stock_bytes+pure_bytes[:7]+b'\x04\x15\0'+pure_bytes[8:]
    for name in ['bbp_candidate_attr_read','bbp_gamepad_ble_send','bbp_trial_connection_reset',
                 'zmk_hog_send_keyboard_report','zmk_hog_send_consumer_report','zmk_hog_send_mouse_report']:
        image.symbol(name)
    cb=image.symbol(CALLBACK);at=cb['st_value'];size=cb['st_size']
    lo=image.address('_bt_conn_cb_list_start');hi=image.address('_bt_conn_cb_list_end')
    require(size>=8 and size%4==0 and hi>lo and (hi-lo)%size==0 and lo<=at and at+size<=hi and (at-lo)%size==0,
            'trial connection callbacks not in iterable')
    connected,disconnected=struct.unpack('<II',image.read(at,8))
    pointer(connected,'bbp_trial_connected');pointer(disconnected,'bbp_trial_disconnected')
    return dict(hids_attributes=29,stock_attributes_retained=22,virtual_map_bytes=len(virtual),
                virtual_map_sha256=hashlib.sha256(virtual).hexdigest(),
                input_id=4,descriptor_input_bytes=10,output_id=5,descriptor_output_bytes=4,
                live_report_lengths_verified=False,live_map_read_verified=False,
                standard_ccc_write=True,lifecycle_iterable_verified=True)


def check_commands(entries, stage=None):
    result={}
    for name in ['hog.c','gamepad_runtime.c','composite_trial_lifecycle.c']:
        matches=[x for x in entries if Path(x['file']).name==name]
        require(len(matches)==1,'expected exactly one TU: '+name)
        if stage is not None:
            expected={'hog.c':'zmk/app/src/hog.c','gamepad_runtime.c':BOARD+'/custom_driver/gamepad_runtime.c',
                      'composite_trial_lifecycle.c':'experiments/gamepad_protocol/composite_trial_lifecycle.c'}[name]
            source=Path(matches[0]['file'])
            if not source.is_absolute(): source=Path(matches[0]['directory'])/source
            require(source.resolve()==(stage/expected).resolve(),'compile source outside exact stage: '+name)
        tokens=matches[0].get('arguments') or shlex.split(matches[0]['command'])
        require(('-DBBP9981_COMPOSITE_DEVICE_TRIAL=1' in tokens)==(name=='hog.c'),
                'trial define must affect HOG TU only: '+name)
        if name=='composite_trial_lifecycle.c':
            require('-DBBP9981_COMPOSITE_DEVICE_TRIAL_LIFECYCLE=1' in tokens,'missing lifecycle-only guard')
        includes=[tokens[i+1] for i,t in enumerate(tokens[:-1]) if t=='-include']
        trial=[p for p in includes if Path(p).name=='composite_ble_trial.h']
        snapshot=[p for p in includes if Path(p).name=='trackpad_hog_snapshot.h']
        require(len(trial)==len(snapshot)==(1 if name=='hog.c' else 0),'trial/snapshot must affect HOG TU only')
        if name=='hog.c': require(includes.index(snapshot[0])<includes.index(trial[0]),'snapshot must precede trial')
        require(not any(Path(p).name in ('gamepad_ble_extension.h','composite_ble_candidate.h','pure_hog_guard.h') for p in includes),'legacy prefix remains')
        result[name]=matches[0]['file']
    for entry in entries:
        tokens=entry.get('arguments') or shlex.split(entry['command'])
        if Path(entry['file']).name!='hog.c':
            require('-DBBP9981_COMPOSITE_DEVICE_TRIAL=1' not in tokens,'HOG trial define leaked to another TU')
            require(not any('composite_ble_trial.h' in p or 'trackpad_hog_snapshot.h' in p for p in tokens),'HOG preinclude leaked to another TU')
        if Path(entry['file']).name!='composite_trial_lifecycle.c':
            require('-DBBP9981_COMPOSITE_DEVICE_TRIAL_LIFECYCLE=1' not in tokens,'lifecycle define leaked to another TU')
    return result


def check_manifest(manifest,stage,commit,allow_dirty=False):
    require(len(commit)==40 and all(c in '0123456789abcdef' for c in commit),'invalid source commit')
    require(manifest['source_commit']==commit,'wrong stage commit')
    require(allow_dirty or manifest.get('source_dirty') is False,'dirty source requires explicit local-only opt-in')
    for flag in ['safety_review_passed','experimental_device_trial_accepted','hid_database_change_acknowledged','fresh_configure_required']:
        require(manifest.get(flag) is True,'missing trial gate: '+flag)
    require(manifest.get('profile')=='ble-composite-trial','wrong profile')
    for flag in ['pnp_vid_pid_changed','pnp_source_changed','bluetooth_address_configuration_changed','usb_config_changed','keymap_changed','settings_changed']:
        require(manifest.get(flag) is False,'forbidden source change: '+flag)
    expected={BOARD+'/cmake/gamepad.cmake',BOARD+'/CMakeLists.txt'}
    changes=[x['path'] for x in manifest['modified_config']]
    require(len(changes)==2 and set(changes)==expected,'unexpected staged config changes')
    require(bool(manifest['files']),'empty source manifest')
    for name,meta in manifest['files'].items():
        path=Path(name)
        require(not path.is_absolute() and '..' not in path.parts,'unsafe manifest path')
        source=stage/path
        require(source.resolve().is_relative_to(stage.resolve()) and not source.is_symlink(),'unsafe staged source')
        raw=source.read_bytes()
        require(len(raw)==meta['bytes'] and hashlib.sha256(raw).hexdigest()==meta['sha256'],'staged source changed: '+name)


def check_config(config):
    values={}
    for line in config.splitlines():
        if line.startswith('CONFIG_') and '=' in line:
            key,value=line.split('=',1)
            require(key not in values,'duplicate config key');values[key]=value
    for key in ['BBP9981_GAMEPAD','ZMK_BLE','ZMK_USB','A320_TAP_OUTPUT','ZMK_POINTING','ZMK_HID_INDICATORS',
                'BT_GATT_ENFORCE_SUBSCRIPTION','ZMK_HID_REPORT_TYPE_HKRO','ZMK_HID_CONSUMER_REPORT_USAGES_FULL']:
        require(values.get('CONFIG_'+key)=='y','required config disabled: '+key)
    for key in ['BT_EATT','ZMK_POINTING_SMOOTH_SCROLLING']:
        require(values.get('CONFIG_'+key) in (None,'n'),'forbidden config: '+key)
    if values.get('CONFIG_BT_SETTINGS_CCC_LAZY_LOADING')=='y':
        return int(values['CONFIG_BT_MAX_CONN'])
    require(values.get('CONFIG_BT_CONN')=='y','connection support missing')
    return int(values['CONFIG_BT_MAX_PAIRED'])+int(values['CONFIG_BT_MAX_CONN'])


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['build','stage','out']: p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--commit',required=True);p.add_argument('--run-id',type=int,required=True)
    p.add_argument('--allow-dirty-local-source',action='store_true')
    args=p.parse_args()
    require(args.run_id>0,'invalid run provenance')
    manifest=json.loads((args.stage/'CANDIDATE.json').read_text())
    check_manifest(manifest,args.stage,args.commit,args.allow_dirty_local_source)
    z=args.build/'zephyr';config=(z/'.config').read_text();ccc_slots=check_config(config)
    commands=check_commands(json.loads((args.build/'compile_commands.json').read_text()),args.stage)
    require((args.build/'bbp-composite-trial-configured').is_file(),'missing fresh-configure marker')
    standard=check_standard(config,(z/'zephyr.dts').read_text())
    from elftools.elf.elffile import ELFFile
    with (z/'zmk.elf').open('rb') as stream:
        image=Image(ELFFile(stream));structure=check_elf(image,ccc_slots);pnp=check_pnp(image,config,1)
    require(structure['virtual_map_sha256']==manifest['expected_map_sha256'],'manifest map mismatch')
    uf2=check_uf2_matches(image,(z/'zmk.uf2').read_bytes())
    result=dict(profile='ble-composite-trial',source_commit=args.commit,source_run=args.run_id,
                local_dirty_source=manifest['source_dirty'],device_tested=False,flashable_delivery=False,
                bluetooth_keyboard_mouse=True,elf=structure,pnp=pnp,uf2=uf2,compiled_sources=commands,
                standard_baseline=standard)
    args.out.parent.mkdir(parents=True,exist_ok=True)
    with args.out.open('x') as out: json.dump(result,out,indent=2);out.write('\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__': main()
