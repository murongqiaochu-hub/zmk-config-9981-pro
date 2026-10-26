#!/usr/bin/env python3
"""Read pinned nRF52840/Zephyr static GATT ABI from a UF2, without device access.

ARM attrs are 20 bytes, service descriptors 8. UUID compound literals reside in
initialized RAM; infer their ROM-load/VMA displacement using the unique stock
22-attribute HIDS. Reject ambiguous layouts rather than guessing handles.
"""
from pathlib import Path
import struct, json, sys

def inspect(raw):
    blocks=[]
    for o in range(0,len(raw),512):
        addr,size=struct.unpack_from('<II',raw,o+12)
        assert size==256
        blocks.append((addr,raw[o+32:o+32+size]))
    blocks.sort();base=blocks[0][0]
    assert all(a==base+i*256 for i,(a,_) in enumerate(blocks))
    data=b''.join(v for _,v in blocks);end=base+len(data)
    def word(a):
        assert base<=a<=end-4
        return struct.unpack_from('<I',data,a-base)[0]
    hids_literals=[base+i for i in range(len(data)-3) if data[i:i+4]==b'\x00\x00\x12\x18']
    results=[]
    for i in range(0,len(data)-20,4):
        u,rd,wr,ud,hp=struct.unpack_from('<IIIII',data,i)
        attr=base+i
        if not (0x20000000<=u<0x20040000 and 0x20000000<=ud<0x20040000 and wr==0 and rd&1 and base<rd<end and hp==0x10000):continue
        # 22 stock attributes, or 26 once the Game Pad report is appended to
        # that same HID service (v2.3.3.2). Any other count means another ABI.
        needle=None
        for candidate in (22,26):
            probe=struct.pack('<II',attr,candidate)
            if probe in data:needle=probe;break
        if needle is None:continue
        for literal in hids_literals:
            delta=ud-literal
            def load_addr(a):return a-delta if 0x20000000<=a<0x20040000 else a
            def uuid(a):
                a=load_addr(a)
                if not base<=a<=end-4:return None
                o=a-base;typ=data[o]
                if typ==0:return '16:%04x'%struct.unpack_from('<H',data,o+2)[0]
                if typ==1 and a<=end-8:return '32:%08x'%struct.unpack_from('<I',data,o+4)[0]
                if typ==2 and a<=end-17:return '128:'+data[o+1:o+17].hex()
                return None
            def service(a):
                if not base<=a<=end-8:return None
                ptr,n=struct.unpack_from('<II',data,a-base)
                if not (1<=n<=128 and base<=ptr<=end-20*n):return None
                if uuid(word(ptr))!='16:2800':return None
                ident=uuid(word(ptr+12))
                if ident is None:return None
                attrs=[]
                for ix in range(n):
                    au,ar,aw,adat,hperm=struct.unpack_from('<IIIII',data,ptr-base+ix*20)
                    uid=uuid(au)
                    if uid is None or hperm&0xffff or hperm>>16>0x1ff:return None
                    if any(f and not (base<f<end and f&1) for f in [ar,aw]):return None
                    attrs.append({'uuid':uid,'perm':hperm>>16})
                return {'uuid':ident,'count':n,'attrs':attrs}
            if uuid(u)!='16:2800' or uuid(ud)!='16:1812':continue
            obj=base+data.index(needle);start=obj
            while service(start-8):start-=8
            table=[];pos=start;handle=1
            while service(pos):
                s=service(pos);s.update(first=handle,last=handle+s['count']-1)
                table.append(s);handle+=s['count'];pos+=8
            if len(table)<5 or [s['uuid'] for s in table[:2]]!=['16:1801','16:1800']:continue
            if not any(s['uuid']=='16:1812' and s['count'] in (22,26) for s in table):continue
            result={'table_flash_address':hex(start),'uuid_ram_rom_displacement':hex(delta),'services':table}
            if result not in results:results.append(result)
    assert len(results)==1, ('ambiguous or unsupported pinned static GATT layout',len(results))
    return results[0]

if __name__=='__main__':
    print(json.dumps(inspect(Path(sys.argv[1]).read_bytes()),indent=2))
