import importlib.util
from pathlib import Path
import struct

root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('uf2_validator',root/'tools/verify_firmware.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
def block(addr=0x1000, flags=0x2000, size=256, number=0,total=1,family=0xADA52840):
    data=bytearray(512)
    struct.pack_into('<8I',data,0,0x0A324655,0x9E5D5157,flags,addr,size,number,total,family)
    struct.pack_into('<I',data,508,0x0AB16F30)
    return bytes(data)
valid=module.validate_uf2(block());assert valid['blocks']==1
invalid=[b'',b'bad',block(addr=0),block(addr=0xD4000),block(addr=0xF4000),
         block(addr=0x100000),block(addr=0x1001),block(flags=0x2001),block(flags=0x3000),
         block(size=512),block(number=1),block(total=2),block(family=0),
         block(addr=0x2000),block(total=2)+block(addr=0x1200,number=1,total=2)]
for data in invalid:
    try:module.validate_uf2(data)
    except ValueError:pass
    else:raise AssertionError('Unsafe UF2 accepted')
old=root/'firmware-delivery/9743a17/bbp9981-zmk.uf2'
if old.exists():
    assert module.validate_uf2(old.read_bytes())['sha256']=='8d23aeecdc2d6b85827964cf77ea1817360c97414dbaff5232a4b2378cd4fd8c'
print('PASS UF2 validator: rejects empty/corrupt/NOFLASH/container/misaligned/bootloader/NVS/out-of-range blocks; archived baseline valid')
