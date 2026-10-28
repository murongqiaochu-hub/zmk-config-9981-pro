#!/usr/bin/env python3
"""Synthetic ARM-layout negatives for the final-ELF gate; NOT an ARM build."""
from pathlib import Path
import copy
import re
import struct
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from verify_pure_candidate import (Image, check_elf, check_commands, check_uf2_matches,
                                   file_backed_flash, check_pnp, check_profile,
                                   BASE_CHANGES, PNP_CONFIG, UUIDS)

def fixture():
    image=Image.__new__(Image)
    data=bytearray(8192);image.segments=[(0x1000,data)];image.symbols={}
    def put(address,fmt,*values):
        struct.pack_into(fmt,data,address-0x1000,*values)
    def symbol(name,address,size=0):
        image.symbols[name]=[dict(st_value=address,st_size=size)]
    symbol('_bt_gatt_service_static_list_start',0x1100)
    symbol('_bt_gatt_service_static_list_end',0x1108)
    symbol('bbp_exp_hids',0x1100);symbol('attr_bbp_exp_hids',0x1200)
    put(0x1100,'<II',0x1200,16)
    for i,uuid in enumerate(UUIDS):
        put(0x1400+i*4,'<BBH',0,0,uuid)
        user={0:0x1500,10:0x1510,13:0x1512,7:0x1520,11:0x1530}.get(i,0)
        put(0x1200+i*20,'<IIIIHH',0x1400+i*4,0x2001,0x2001,user,0,12)
    put(0x1500,'<BBH',0,0,0x1812)
    put(0x1510,'4B',3,1,5,2)
    put(0x1526,'B',0x12);put(0x1536,'B',0x0e)
    source=(ROOT/'experiments/gamepad_protocol/stadia_descriptor.h').read_bytes()
    body=re.search(rb'bbp_exp_report_map\[\]\s*=\s*\{([^}]+)\}',source)[1]
    descriptor=bytes(int(x,16) for x in re.findall(rb'0x([0-9a-fA-F]{2})',body))
    data[0x600:0x600+len(descriptor)]=descriptor
    symbol('bbp_exp_report_map',0x1600,182)
    for name in ['bbp_gamepad_ble_send','zmk_hog_send_keyboard_report','zmk_hog_send_consumer_report',
                 'zmk_hog_send_mouse_report','bbp_exp_ccc_write','bbp_exp_read_map']:
        symbol(name,0x2001)
    symbol('_bt_conn_cb_list_start',0x1700);symbol('_bt_conn_cb_list_end',0x1740)
    symbol('bt_conn_cb_bbp_exp_connection',0x1700,32)
    symbol('bt_conn_cb_bbp_gamepad_connection',0x1720,32)
    symbol('bbp_exp_on_connected',0x2101);symbol('bbp_exp_on_disconnected',0x2201)
    put(0x1700,'<II',0x2101,0x2201)
    return image,data,put

def reject(call):
    try:
        call()
    except ValueError:
        return
    raise AssertionError('malformed candidate accepted')

def pnp_fixture():
    image,data,put=fixture()
    image.symbols['_bt_gatt_service_static_list_end'][0]['st_value']=0x1110
    image.symbols['dis_svc']=[dict(st_value=0x1108,st_size=8)]
    image.symbols['dis_pnp_id']=[dict(st_value=0x1b20,st_size=7)]
    image.symbols['read_pnp_id']=[dict(st_value=0x2301,st_size=4)]
    put(0x1108,'<II',0x1800,7)
    for i,uuid in enumerate([0x2800,0x2803,0x2a24,0x2803,0x2a29,0x2803,0x2a50]):
        put(0x1a00+i*4,'<BBH',0,0,uuid)
        user={0:0x1b00,6:0x1b20}.get(i,0)
        put(0x1800+i*20,'<IIIIHH',0x1a00+i*4,0x2301,0,user,0,1)
    put(0x1b00,'<BBH',0,0,0x180a)
    put(0x1b20,'<BHHH',1,0x1d50,0x615e,1)
    config='\n'.join(['CONFIG_BT_DIS_PNP=y','CONFIG_BT_DIS_PNP_VID_SRC=1',
        'CONFIG_BT_DIS_PNP_VID=0x1D50','CONFIG_BT_DIS_PNP_PID=0x615E',
        'CONFIG_BT_DIS_PNP_VER=1','CONFIG_USB_DEVICE_VID=0x1D50','CONFIG_USB_DEVICE_PID=0x615E'])
    return image,data,put,config


def test_pnp_profiles():
    manifest=dict(modified_config=[dict(path=p) for p in sorted(BASE_CHANGES)],
        identity_changed=False,usb_changed=False,keymap_changed=False,bluetooth_keyboard_mouse=False)
    check_profile(manifest,False);reject(lambda:check_profile(manifest,True))
    experiment=copy.deepcopy(manifest);experiment['identity_changed']=True
    experiment['experimental_pnp_vendor_source']=2;experiment['modified_config'].append(dict(path=PNP_CONFIG))
    check_profile(experiment,True);reject(lambda:check_profile(experiment,False))
    bad=copy.deepcopy(experiment);bad['modified_config'].append(dict(path=PNP_CONFIG));reject(lambda:check_profile(bad,True))
    bad=copy.deepcopy(experiment);bad['usb_changed']=True;reject(lambda:check_profile(bad,True))
    bad=copy.deepcopy(experiment);del bad['experimental_pnp_vendor_source'];reject(lambda:check_profile(bad,True))
    image,data,put,config=pnp_fixture()
    assert check_pnp(image,config,1)['raw_hex']=='01501d5e610100'
    reject(lambda:check_pnp(image,config,2))
    put(0x1b20,'B',2);config=config.replace('CONFIG_BT_DIS_PNP_VID_SRC=1','CONFIG_BT_DIS_PNP_VID_SRC=2')
    assert check_pnp(image,config,2)['raw_hex']=='02501d5e610100'
    put(0x1b21,'B',0);reject(lambda:check_pnp(image,config,2))
    put(0x1b21,'B',0x50);put(0x1800+6*20+4,'<I',0x2201);reject(lambda:check_pnp(image,config,2))
    print('PASS source-1/source-2 profile isolation and GATT PnP config/object/callback negative checks; synthetic only')


def test_section_mapping():
    from types import SimpleNamespace
    class Section(dict):
        def data(self):
            assert self['sh_type']!='SHT_NOBITS'
            return self['contents']
    segments=[dict(p_type='PT_LOAD',p_vaddr=0x1000,p_paddr=0x1000,p_filesz=16),
              dict(p_type='PT_LOAD',p_vaddr=0x20000000,p_paddr=0x1010,p_filesz=4)]
    sections=[Section(sh_flags=2,sh_type='SHT_PROGBITS',sh_addr=0x1000,sh_size=4,contents=b'code'),
              Section(sh_flags=2,sh_type='SHT_NOBITS',sh_addr=0x1004,sh_size=4,contents=b''),
              Section(sh_flags=2,sh_type='SHT_PROGBITS',sh_addr=0x1008,sh_size=4,contents=b'more'),
              Section(sh_flags=2,sh_type='SHT_PROGBITS',sh_addr=0x20000000,sh_size=4,contents=b'\0'*4),
              Section(sh_flags=0,sh_type='SHT_PROGBITS',sh_addr=0,sh_size=4,contents=b'dbug')]
    elf=SimpleNamespace(iter_segments=lambda:iter(segments),iter_sections=lambda:iter(sections))
    assert file_backed_flash(elf)==[(0x1000,b'code'),(0x1008,b'more'),(0x1010,b'\0'*4)]
    sections[0]['sh_addr']=0x9999;reject(lambda:file_backed_flash(elf))

def main():
    test_pnp_profiles()
    test_section_mapping()
    image,_,_=fixture();assert check_elf(image)['hids_attributes']==16
    for mode in ['two_hids','no_hids','count','uuid','input_ref','output_ref','map','permissions','properties','duplicate_symbol','wrong_callback','unregistered_connection','wrong_connected','wrong_disconnected']:
        image,data,put=fixture()
        if mode=='two_hids':
            image.symbols['_bt_gatt_service_static_list_end'][0]['st_value']=0x1110
            put(0x1108,'<II',0x1200,16)
        elif mode=='no_hids': put(0x1502,'<H',0x1813)
        elif mode=='count': put(0x1104,'<I',15)
        elif mode=='uuid': put(0x1400+9*4+2,'<H',0x2908)
        elif mode=='input_ref': put(0x1510,'B',4)
        elif mode=='output_ref': put(0x1513,'B',1)
        elif mode=='map': data[0x600]^=1
        elif mode=='permissions': put(0x1200+8*20+18,'<H',0)
        elif mode=='properties': put(0x1526,'B',2)
        elif mode=='wrong_callback': put(0x1200+6*20+4,'<I',0x2201)
        elif mode=='unregistered_connection': image.symbols['_bt_conn_cb_list_start'][0]['st_value']=0x1720
        elif mode=='wrong_connected': put(0x1700,'<I',0x2201)
        elif mode=='wrong_disconnected': put(0x1704,'<I',0x2101)
        else: image.symbols['bbp_gamepad_ble_send']*=2
        reject(lambda:check_elf(image))
    entries=[dict(file='/candidate/'+name,arguments=['cc','-DBBP9981_PURE_BLE_CANDIDATE=1']+
                  (['-include','/candidate/pure_hog_guard.h'] if name=='hog.c' else []))
             for name in ['hog.c','pure_runtime.c','gamepad_runtime.c']]
    assert len(check_commands(entries))==3
    bad=copy.deepcopy(entries);bad[1]['arguments']+=['-include','pure_hog_guard.h'];reject(lambda:check_commands(bad))
    bad=copy.deepcopy(entries);bad[0]['arguments']+=['-include','gamepad_ble_extension.h'];reject(lambda:check_commands(bad))
    bad=copy.deepcopy(entries);bad.append(bad[1]);reject(lambda:check_commands(bad))
    bad=copy.deepcopy(entries);bad[0]['arguments'].remove('-DBBP9981_PURE_BLE_CANDIDATE=1');reject(lambda:check_commands(bad))
    image,data,_=fixture();image.flash=[(0x1000,bytes(data))]
    blocks=[]
    for i in range(len(data)//256):
        block=bytearray(512)
        struct.pack_into('<8I',block,0,0x0a324655,0x9e5d5157,0x2000,0x1000+i*256,256,i,len(data)//256,0xada52840)
        block[32:288]=data[i*256:(i+1)*256];struct.pack_into('<I',block,508,0x0ab16f30)
        blocks.append(block)
    uf2=b''.join(blocks);assert check_uf2_matches(image,uf2)['elf_flash_bytes_matched']==len(data)
    bad=bytearray(uf2);bad[33]^=1;reject(lambda:check_uf2_matches(image,bad))
    image.flash=[(0x1000,bytes(data[:-256]))];reject(lambda:check_uf2_matches(image,uf2))
    workflow=(ROOT/'.github/workflows/main.yml').read_text()
    assert 'options: [standard, pure-ble, pure-ble-usb-pnp]' in workflow
    assert "(inputs.profile != 'pure-ble' && inputs.profile != 'pure-ble-usb-pnp')" in workflow
    assert "(inputs.profile == 'pure-ble' || inputs.profile == 'pure-ble-usb-pnp')" in workflow
    assert "github.ref == 'refs/heads/codex/cross-platform-hid-20261026'" in workflow
    assert "github.repository == 'murongqiaochu-hub/zmk-config-9981-pro'" in workflow
    assert 'pure-ble-usb-pnp) args=(--experiment-usb-pnp-source --accept-pnp-source-change)' in workflow
    assert 'pure-ble-usb-pnp) args=(--experiment-usb-pnp-source) ;;' in workflow
    assert workflow.count("refs/heads/codex/xbox-layout-20261020")==1
    assert workflow.count("HEAD:codex/xbox-layout-20261020")==1
    assert 'persist-credentials: false' in workflow and 'tools/verify_pure_candidate.py' in workflow
    print('PASS pure artifact gate model: 14 malformed ELF layouts, 4 compile-command errors and mismatched/extra-tail UF2 rejected; manual-only branch-scoped profile; NOT ARM/device evidence')

if __name__=='__main__':
    main()
