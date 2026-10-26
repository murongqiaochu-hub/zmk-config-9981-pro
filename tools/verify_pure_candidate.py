#!/usr/bin/env python3
"""Verify the explicit ARM diagnostic artifact. Never publish or touch devices.
Requires pyelftools (already part of the Zephyr build environment).
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import struct
from verify_firmware import validate_uf2

MAP_SHA='4317e34b66d9c247b0fb12327516c38c74deea04619db54a05cf190444482a3f'
UUIDS=[0x2800,0x2803,0x2a4e,0x2803,0x2a4a,0x2803,0x2a4b,
       0x2803,0x2a4d,0x2902,0x2908,0x2803,0x2a4d,0x2908,0x2803,0x2a4c]

def require(condition,message):
    if not condition:
        raise ValueError(message)

def file_backed_flash(elf):
    # objcopy fills NOBITS/alignment holes with 0xff; raw PT_LOAD file padding
    # can instead be zero. Compare actual allocated file-backed sections, using
    # PT_LOAD only to translate each VMA to its flash load address (LMA).
    loads=[s for s in elf.iter_segments() if s['p_type']=='PT_LOAD']
    result=[]
    for section in elf.iter_sections():
        if not section['sh_flags']&2 or section['sh_type']=='SHT_NOBITS' or not section['sh_size']:
            continue
        address=section['sh_addr'];size=section['sh_size']
        matches=[s for s in loads if s['p_vaddr']<=address and address+size<=s['p_vaddr']+s['p_filesz']]
        require(len(matches)==1,'file-backed alloc section must map to one PT_LOAD')
        segment=matches[0];physical=segment['p_paddr']+address-segment['p_vaddr']
        require(0x1000<=physical and physical+size<=0xd4000,'ELF load section outside application flash')
        data=section.data();require(len(data)==size,'section data size mismatch')
        result.append((physical,data))
    return result

class Image:
    def __init__(self,elf):
        require(elf.elfclass==32 and elf.little_endian and elf['e_machine']=='EM_ARM','expected little-endian ARM32 ELF')
        self.segments=[(s['p_vaddr'],s.data()) for s in elf.iter_segments() if s['p_type']=='PT_LOAD']
        self.flash=file_backed_flash(elf)
        table=elf.get_section_by_name('.symtab')
        require(table is not None,'unstripped ELF symbol table required')
        self.symbols={}
        for symbol in table.iter_symbols():
            if symbol.name and symbol['st_shndx']!='SHN_UNDEF':
                self.symbols.setdefault(symbol.name,[]).append(symbol.entry)
    def symbol(self,name):
        entries=self.symbols.get(name,[])
        require(len(entries)==1,'expected one symbol: '+name)
        return entries[0]
    def address(self,name):
        return self.symbol(name)['st_value']
    def read(self,address,size):
        for start,data in self.segments:
            if start<=address and address+size<=start+len(data):
                return data[address-start:address-start+size]
        raise ValueError(f'ELF read outside loadable data: {address:#x}+{size}')
    def uuid16(self,address):
        data=self.read(address,4)
        return struct.unpack_from('<H',data,2)[0] if data[0]==0 else None
    def attr(self,address):
        uuid,read,write,user,handle,perm=struct.unpack('<IIIIHH',self.read(address,20))
        return dict(uuid=self.uuid16(uuid),read=read,write=write,user=user,handle=handle,perm=perm)

def check_elf(image):
    start=image.address('_bt_gatt_service_static_list_start')
    end=image.address('_bt_gatt_service_static_list_end')
    require(end>start and (end-start)%8==0,'invalid GATT iterable boundaries')
    hid=[];services=[]
    for position in range(start,end,8):
        attrs,count=struct.unpack('<II',image.read(position,8))
        require(0<count<1024,'invalid service attribute count')
        first=image.attr(attrs)
        require(first['uuid']==0x2800,'expected primary service at iterable entry')
        uuid=image.uuid16(first['user'])
        services.append(dict(address=hex(position),uuid16=uuid,attributes=count))
        if uuid==0x1812:
            hid.append((position,attrs,count))
    require(len(hid)==1,'candidate must register exactly ONE HID service')
    position,attrs,count=hid[0]
    require(position==image.address('bbp_exp_hids') and count==16,'wrong registered HIDS or attr count')
    require(attrs==image.address('attr_bbp_exp_hids'),'HIDS uses unexpected attribute table')
    fields=[image.attr(attrs+i*20) for i in range(count)]
    require([x['uuid'] for x in fields]==UUIDS,'unexpected pure HIDS attribute order')
    require(image.read(fields[10]['user'],2)==bytes([3,1]),'input ReportRef must be ID3/Input')
    require(image.read(fields[13]['user'],2)==bytes([5,2]),'output ReportRef must be ID5/Output')
    require(fields[8]['perm']&4 and fields[9]['perm']&12==12 and fields[12]['perm']&12==12,'missing encrypted report/CCC permissions')
    require(image.read(fields[7]['user']+6,1)==bytes([0x12]),'input must be READ|NOTIFY')
    require(image.read(fields[11]['user']+6,1)==bytes([0x0e]),'output must be READ|WRITE|WRITE_WITHOUT_RESP')
    require(fields[6]['read']&~1==image.address('bbp_exp_read_map')&~1,'Report Map attribute uses wrong callback')
    descriptor=image.symbol('bbp_exp_report_map')
    require(descriptor['st_size']==182,'wrong map size')
    require(hashlib.sha256(image.read(descriptor['st_value'],182)).hexdigest()==MAP_SHA,'wrong immutable Map')
    for name in ['bbp_gamepad_ble_send','zmk_hog_send_keyboard_report','zmk_hog_send_consumer_report',
                 'zmk_hog_send_mouse_report','bbp_exp_ccc_write']:
        image.symbol(name)
    # Pinned BT_CONN_CB_DEFINE prefixes the user name with bt_conn_cb_. Verify
    # actual iterable membership and function pointers, not merely name presence.
    cb_start=image.address('_bt_conn_cb_list_start');cb_end=image.address('_bt_conn_cb_list_end')
    callback=image.symbol('bt_conn_cb_bbp_exp_connection')
    address=callback['st_value'];size=callback['st_size']
    require(size>=8 and size%4==0 and cb_end>cb_start and (cb_end-cb_start)%size==0,'invalid callback iterable layout')
    for name in ['bt_conn_cb_bbp_exp_connection','bt_conn_cb_bbp_gamepad_connection']:
        item=image.symbol(name);at=item['st_value']
        require(item['st_size']==size and cb_start<=at and at+size<=cb_end and (at-cb_start)%size==0,'callback not registered: '+name)
    connected,disconnected=struct.unpack('<II',image.read(address,8))
    require(connected&~1==image.address('bbp_exp_on_connected')&~1,'wrong connected callback')
    require(disconnected&~1==image.address('bbp_exp_on_disconnected')&~1,'wrong disconnected callback')
    return dict(services=services,hids_address=hex(position),hids_attributes=count,
                connection_callback_address=hex(address),callback_record_bytes=size,
                registered_connection_callbacks=(cb_end-cb_start)//size,
                report_map_sha256=MAP_SHA,input_id=3,descriptor_input_bytes=10,output_id=5,
                descriptor_output_bytes=4,live_report_lengths_verified=False)

def check_commands(entries):
    result={}
    for name in ['hog.c','pure_runtime.c','gamepad_runtime.c']:
        matches=[x for x in entries if Path(x['file']).name==name]
        require(len(matches)==1,'expected exactly one compile command for '+name)
        tokens=matches[0].get('arguments') or shlex.split(matches[0]['command'])
        require('-DBBP9981_PURE_BLE_CANDIDATE=1' in tokens,'candidate compile guard missing: '+name)
        text=' '.join(tokens)
        require('gamepad_ble_extension.h' not in text and 'trackpad_hog_snapshot.h' not in text,'legacy HOG prefix remains')
        require(text.count('pure_hog_guard.h')==(1 if name=='hog.c' else 0),'guard must affect hog TU only')
        result[name]=matches[0]['file']
    return result

def check_uf2_matches(image,data):
    result=validate_uf2(data)
    payload=b''.join(data[i+32:i+288] for i in range(0,len(data),512))
    require(bool(image.flash),'ELF has no application flash segments')
    expected_end=max(address+len(segment) for address,segment in image.flash)
    require(len(payload)==((expected_end-0x1000+255)//256)*256,'UF2 has unexpected extra/missing tail blocks')
    covered=bytearray(len(payload));verified=0
    for address,segment in image.flash:
        offset=address-0x1000
        require(payload[offset:offset+len(segment)]==segment,'UF2 does not match verified ELF flash bytes')
        covered[offset:offset+len(segment)]=b'\1'*len(segment)
        verified+=len(segment)
    require(all(mark or value in (0,255) for mark,value in zip(covered,payload)),'UF2 contains non-padding data outside ELF flash segments')
    result['elf_flash_bytes_matched']=verified
    result['comparison_basis']='allocated file-backed sections at flash LMA; only NOBITS/alignment/tail padding may be 00/ff'
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--stage',type=Path,required=True)
    parser.add_argument('--commit',required=True)
    parser.add_argument('--run-id',type=int,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    from elftools.elf.elffile import ELFFile
    require(len(args.commit)==40 and all(c in '0123456789abcdef' for c in args.commit) and args.run_id>0,'invalid provenance')
    manifest=json.loads((args.stage/'CANDIDATE.json').read_text())
    require(manifest['source_commit']==args.commit and not manifest['source_dirty'],'stage source provenance mismatch')
    require(manifest['report_map_sha256']==MAP_SHA,'manifest map mismatch')
    for name,metadata in manifest['files'].items():
        require(hashlib.sha256((args.stage/name).read_bytes()).hexdigest()==metadata['sha256'],'staged file changed: '+name)
    zephyr=args.build/'zephyr'
    config=(zephyr/'.config').read_text()
    for key in ['CONFIG_BBP9981_GAMEPAD','CONFIG_ZMK_BLE','CONFIG_ZMK_USB','CONFIG_A320_TAP_OUTPUT','CONFIG_ZMK_POINTING']:
        require(key+'=y' in config.splitlines(),'required config disabled: '+key)
    commands=check_commands(json.loads((args.build/'compile_commands.json').read_text()))
    with (zephyr/'zmk.elf').open('rb') as stream:
        image=Image(ELFFile(stream))
        structure=check_elf(image)
    uf2=check_uf2_matches(image,(zephyr/'zmk.uf2').read_bytes())
    result=dict(source_commit=args.commit,source_run=args.run_id,device_tested=False,
                bluetooth_keyboard_mouse=False,compiled_sources=commands,elf=structure,uf2=uf2)
    args.out.parent.mkdir(parents=True,exist_ok=True)
    with args.out.open('x') as output:
        json.dump(result,output,indent=2);output.write('\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__':
    main()
