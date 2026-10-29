#!/usr/bin/env python3
"""Synthetic ARM-layout and provenance negatives; NOT an ARM build."""
from pathlib import Path
import copy
import re
import struct
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from verify_composite_trial import (Image, UUIDS, CALLBACK, check_elf, check_commands,
    check_config, check_standard, BASELINE)
import json

def reject(call):
    try: call()
    except ValueError: return
    raise AssertionError('malformed trial accepted')

def fixture():
    image=Image.__new__(Image);data=bytearray(0x4000)
    image.segments=[(0x1000,data)];image.symbols={}
    def put(at,fmt,*values): struct.pack_into(fmt,data,at-0x1000,*values)
    def symbol(name,at,size=0): image.symbols[name]=[dict(st_value=at,st_size=size)]
    def function(name):
        if name not in image.symbols: symbol(name,0x4001+len(image.symbols)*4,4)
        return image.address(name)
    symbol('_bt_gatt_service_static_list_start',0x1100);symbol('_bt_gatt_service_static_list_end',0x1108)
    symbol('hog_svc',0x1100,8);symbol('attr_hog_svc',0x1200,580)
    put(0x1100,'II',0x1200,29);put(0x1800,'BBH',0,0,0x1812)
    for i,uuid in enumerate(UUIDS):
        put(0x1600+i*4,'BBH',0,0,uuid)
        put(0x1200+i*20,'IIIIHH',0x1600+i*4,0,0,0x1900+i*16,0,12)
    put(0x1200+12,'I',0x1800)
    for index,ref in [(8,(1,1)),(12,(2,1)),(16,(3,1)),(19,(1,2)),(24,(5,2)),(28,(4,1))]:
        put(0x1900+index*16,'BB',*ref)
    reads={2:'read_hids_info',4:'read_hids_report_map',6:'read_hids_input_report',10:'read_hids_consumer_input_report',14:'read_hids_mouse_input_report',23:'bbp_candidate_read_output',26:'bbp_candidate_read_input',24:'bbp_candidate_read_ref',28:'bbp_candidate_read_ref'}
    for i,name in reads.items(): put(0x1200+i*20+4,'I',function(name))
    for i,name in [(18,'write_hids_leds_report'),(21,'write_ctrl_point'),(23,'bbp_candidate_write_output')]:
        put(0x1200+i*20+8,'I',function(name))
    for i in [7,11,15,27]:
        put(0x1200+i*20+4,'II',function('bt_gatt_attr_read_ccc'),function('bt_gatt_attr_write_ccc'))
    for i,prop in [(5,0x12),(9,0x12),(13,0x12),(17,0x0e),(22,0x0e),(25,0x12)]: put(0x1900+i*16+6,'B',prop)
    put(0x1200+27*20+12,'I',0x2100)
    put(0x2100+24,'III',function('bbp_candidate_ccc_changed'),function('bbp_candidate_ccc_write'),0)
    stock=bytes.fromhex((ROOT/'tests/hid_map_fixtures/stock_196.hex').read_text())
    body=re.search(rb'bbp_exp_report_map\[\]\s*=\s*\{([^}]+)\}',(ROOT/'experiments/gamepad_protocol/stadia_descriptor.h').read_bytes())[1]
    pure=bytes(int(x,16) for x in re.findall(rb'0x([0-9a-fA-F]{2})',body))
    for name,at,raw in [('zmk_hid_report_desc',0x2200,stock),('bbp_exp_report_map',0x2300,pure)]:
        symbol(name,at,len(raw));data[at-0x1000:at-0x1000+len(raw)]=raw
    for name in ['bbp_candidate_attr_read','bbp_gamepad_ble_send','bbp_trial_connection_reset','zmk_hog_send_keyboard_report','zmk_hog_send_consumer_report','zmk_hog_send_mouse_report']: function(name)
    symbol('_bt_conn_cb_list_start',0x2500);symbol('_bt_conn_cb_list_end',0x2520)
    symbol(CALLBACK,0x2500,32)
    put(0x2500,'II',function('bbp_trial_connected'),function('bbp_trial_disconnected'))
    return image,data,put

def main():
    baseline=json.loads((BASELINE/'assigned_config.json').read_text())
    config='\n'.join(k+'='+v for k,v in baseline.items())+'\n'
    dts=(BASELINE/'zephyr.dts').read_text()
    assert check_config(config)==5
    assert check_standard(config,dts)['assigned_values']==1170
    reject(lambda:check_standard(config.replace('CONFIG_BT_MAX_CONN=5','CONFIG_BT_MAX_CONN=4'),dts))
    reject(lambda:check_standard(config+'CONFIG_UNREVIEWED_FLAG=y\n',dts))
    reject(lambda:check_standard(config,dts.replace('model = "bbp9981"','model = "other"')))
    reject(lambda:check_standard(config,dts.replace('&cursor_speed','&none')))
    reject(lambda:check_config(config+'CONFIG_BT_EATT=y\n'))
    image,_,_=fixture();assert check_elf(image,2)['hids_attributes']==29
    # Fixed zmk/hid.h is static const: USB and HOG retain local same-name copies.
    # All copies must be checked, not just whichever comes first in .symtab.
    image,data,_=fixture()
    data[0x1700:0x1700+196]=data[0x1200:0x1200+196]
    image.symbols['zmk_hid_report_desc'].append(dict(st_value=0x2700,st_size=196))
    assert check_elf(image,2)['stock_descriptor_copies_verified']==2
    for index in (0,1):
        bad=copy.deepcopy(image);at=bad.symbols['zmk_hid_report_desc'][index]['st_value']-0x1000
        bad.segments[0][1][at]^=1
        reject(lambda:check_elf(bad,2))
    bad=copy.deepcopy(image);bad.symbols['zmk_hid_report_desc'][1]['st_size']=195
    reject(lambda:check_elf(bad,2))
    bad=copy.deepcopy(image);bad.symbols.pop('zmk_hid_report_desc')
    reject(lambda:check_elf(bad,2))
    for mode in ['two_hid','count','uuid','ref','stock','pure','ccc_wrapper','cfg_write','cfg_match','connected','iterable','led','properties','map_callback']:
        image,data,put=fixture()
        if mode=='two_hid':
            image.symbols['_bt_gatt_service_static_list_end'][0]['st_value']=0x1110;put(0x1108,'II',0x1200,29)
        elif mode=='count': put(0x1104,'I',28)
        elif mode=='uuid': put(0x1600+27*4+2,'H',0x2908)
        elif mode=='ref': put(0x1900+28*16,'B',3)
        elif mode=='stock': data[0x1200]^=1
        elif mode=='pure': data[0x1300]^=1
        elif mode=='ccc_wrapper': put(0x1200+27*20+8,'I',0x4999)
        elif mode=='cfg_write': put(0x2100+28,'I',0x4999)
        elif mode=='cfg_match': put(0x2100+32,'I',1)
        elif mode=='connected': put(0x2500,'I',0x4999)
        elif mode=='iterable': image.symbols[CALLBACK][0]['st_value']=0x2600
        elif mode=='led': put(0x1200+18*20+8,'I',0)
        elif mode=='properties': put(0x1900+25*16+6,'B',2)
        elif mode=='map_callback': put(0x1200+4*20+4,'I',0)
        reject(lambda:check_elf(image,2))
    entries=[dict(file='/trial/'+name,arguments=['cc']+(['-DBBP9981_COMPOSITE_DEVICE_TRIAL=1'] if name=='hog.c' else [])+(['-include','/trial/trackpad_hog_snapshot.h','-include','/trial/composite_ble_trial.h'] if name=='hog.c' else [])) for name in ['hog.c','gamepad_runtime.c','composite_trial_lifecycle.c']]
    entries[2]['arguments'].append('-DBBP9981_COMPOSITE_DEVICE_TRIAL_LIFECYCLE=1')
    assert len(check_commands(entries))==3
    bad=copy.deepcopy(entries);bad[0]['arguments'][-1],bad[0]['arguments'][-3]=bad[0]['arguments'][-3],bad[0]['arguments'][-1];reject(lambda:check_commands(bad))
    bad=copy.deepcopy(entries);bad[1]['arguments']+=['-include','composite_ble_trial.h'];reject(lambda:check_commands(bad))
    bad=copy.deepcopy(entries);bad.append(bad[0]);reject(lambda:check_commands(bad))
    bad=copy.deepcopy(entries);bad[0]['arguments'].remove('-DBBP9981_COMPOSITE_DEVICE_TRIAL=1');reject(lambda:check_commands(bad))
    bad=copy.deepcopy(entries);bad[1]['arguments'].append('-DBBP9981_COMPOSITE_DEVICE_TRIAL=1');reject(lambda:check_commands(bad))
    bad=copy.deepcopy(entries);bad.append(dict(file='/trial/other.c',arguments=['cc','-DBBP9981_COMPOSITE_DEVICE_TRIAL=1']));reject(lambda:check_commands(bad))
    bad=copy.deepcopy(entries);bad[0]['arguments'].append('-DBBP9981_COMPOSITE_DEVICE_TRIAL_LIFECYCLE=1');reject(lambda:check_commands(bad))
    print('PASS duplicate local stock descriptors, 18 malformed ELF layouts and 7 compile-command negatives; synthetic only')
if __name__=='__main__': main()
