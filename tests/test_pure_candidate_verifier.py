#!/usr/bin/env python3
"""Synthetic ARM-layout negatives for the final-ELF gate; NOT an ARM build."""
from pathlib import Path
import copy
import re
import struct
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from verify_pure_candidate import Image, check_elf, check_commands, check_uf2_matches, UUIDS

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
                 'zmk_hog_send_mouse_report','bbp_exp_connection','bbp_exp_ccc_write','bbp_exp_read_map']:
        symbol(name,0x2001)
    return image,data,put

def reject(call):
    try:
        call()
    except ValueError:
        return
    raise AssertionError('malformed candidate accepted')

def main():
    image,_,_=fixture();assert check_elf(image)['hids_attributes']==16
    for mode in ['two_hids','no_hids','count','uuid','input_ref','output_ref','map','permissions','properties','duplicate_symbol','wrong_callback']:
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
    assert "inputs.profile == 'pure-ble'" in workflow and "inputs.profile != 'pure-ble'" in workflow
    assert "github.ref == 'refs/heads/codex/cross-platform-hid-20261026'" in workflow
    assert 'persist-credentials: false' in workflow and 'tools/verify_pure_candidate.py' in workflow
    print('PASS pure artifact gate model: 11 malformed ELF layouts, 4 compile-command errors and mismatched/extra-tail UF2 rejected; manual-only branch-scoped profile; NOT ARM/device evidence')

if __name__=='__main__':
    main()
