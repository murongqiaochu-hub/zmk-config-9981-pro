"""Reviewed ARM32 ELF and UF2 helpers; no device or publication operations."""
import struct
from verify_firmware import validate_uf2

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

def check_pnp(image, config, expected_source):
    require(expected_source in (1,2),'unsupported PnP vendor source')
    lines=config.splitlines()
    def value(key):
        matches=[line.split('=',1)[1] for line in lines if line.startswith(key+'=')]
        require(len(matches)==1,'expected one config setting: '+key)
        return matches[0]
    require(value('CONFIG_BT_DIS_PNP')=='y','PnP service disabled')
    for name,expected in [('CONFIG_BT_DIS_PNP_VID_SRC',expected_source),('CONFIG_BT_DIS_PNP_VID',0x1d50),
                          ('CONFIG_BT_DIS_PNP_PID',0x615e),('CONFIG_BT_DIS_PNP_VER',1),
                          ('CONFIG_USB_DEVICE_VID',0x1d50),('CONFIG_USB_DEVICE_PID',0x615e)]:
        require(int(value(name),0)==expected,'unexpected config: '+name)
    start=image.address('_bt_gatt_service_static_list_start');end=image.address('_bt_gatt_service_static_list_end')
    require(end>start and (end-start)%8==0,'invalid GATT iterable boundaries')
    found=[]
    for pos in range(start,end,8):
        attrs,count=struct.unpack('<II',image.read(pos,8));require(0<count<1024,'invalid service count')
        first=image.attr(attrs)
        if first['uuid']==0x2800 and image.uuid16(first['user'])==0x180a:
            found.append((pos,attrs,count))
    require(len(found)==1,'expected one registered Device Information service')
    position,attrs,count=found[0]
    require(position==image.address('dis_svc'),'unexpected Device Information service')
    refs=[image.attr(attrs+i*20) for i in range(count)]
    pnp=[attr for attr in refs if attr['uuid']==0x2a50]
    require(len(pnp)==1,'expected one PnP ID characteristic')
    symbol=image.symbol('dis_pnp_id')
    require(symbol['st_size']==7 and pnp[0]['user']==symbol['st_value'],'PnP object reference mismatch')
    require(pnp[0]['read']&~1==image.address('read_pnp_id')&~1,'wrong PnP read callback')
    expected=struct.pack('<BHHH',expected_source,0x1d50,0x615e,1)
    require(image.read(symbol['st_value'],7)==expected,'compiled PnP bytes do not match experiment')
    return dict(vendor_source=expected_source,vendor_id='0x1d50',product_id='0x615e',version=1,
                raw_hex=expected.hex(),source_changed=(expected_source==2),live_device_verified=False)

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
