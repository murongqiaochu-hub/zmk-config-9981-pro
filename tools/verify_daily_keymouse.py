#!/usr/bin/env python3
"""Verify real daily ARM image, pure HOG, exact source, hardware and UF2. Never flash."""
import argparse, hashlib, json, re, shlex, struct, subprocess
from pathlib import Path
from elf_hid_artifact import Image, require, check_pnp, check_uf2_matches
ROOT=Path(__file__).resolve().parents[1]
BASELINE=ROOT/'tools/daily_baseline'
STOCK_SHA='5c7e4106019e5baf9027c8a24e334c58f5e09c65cd3ff46b338c551c61da4ee3'
STOCK_UUIDS=[0x2800,0x2803,0x2a4a,0x2803,0x2a4b,0x2803,0x2a4d,0x2902,0x2908,0x2803,0x2a4d,0x2902,0x2908,0x2803,0x2a4d,0x2902,0x2908,0x2803,0x2a4d,0x2908,0x2803,0x2a4c]
EXPECTED_CONFIG_CHANGES={
 'CONFIG_BBP9981_GAMEPAD':{'before':'y','after':None},
 'CONFIG_DT_HAS_ZMK_BEHAVIOR_GAMEPAD_ENABLED':{'before':'y','after':None},
 'CONFIG_USB_HID_DEVICE_COUNT':{'before':'2','after':'1'},
 'CONFIG_USB_HID_REPORTS':{'before':'4','after':'3'},
}
def assigned(text):
 pairs=re.findall(r'^(CONFIG_\w+)=(.*)$',text,re.M)
 require(len(pairs)==len(dict(pairs)),'duplicate Kconfig value')
 return dict(pairs)
def normal_dts(text):
 require(text.count('/dts-v1/;')==1 and text.rstrip().endswith('};'),'invalid complete DTS')
 text=text[text.index('/dts-v1/;'):].replace('\r\n','\n')
 return re.sub(r'(?m)^[ \t]*phandle = < 0x[0-9a-f]+ >;\n','',text).strip()+'\n'
def check_hardware(config,dts):
 raw=(BASELINE/'assigned_config.json').read_bytes(); rd=(BASELINE/'zephyr.dts').read_bytes()
 require(hashlib.sha256(raw).hexdigest()=='51e29321a412aaa8ce1d27e108fd2fdd030daa2e61cd6ffe120bfeb0190f0ce4','changed Kconfig fixture')
 require(hashlib.sha256(rd).hexdigest()=='77d47ac09693cfafe1f9c69717e5cd520341cbea59d5daf015eaac82cf007f99','changed DTS fixture')
 before=json.loads(raw); after=assigned(config)
 require(len(before)==1170,'wrong baseline count')
 changes={k:{'before':before.get(k),'after':after.get(k)} for k in sorted(set(before)|set(after)) if before.get(k)!=after.get(k)}
 require(changes==EXPECTED_CONFIG_CHANGES,'unexpected full Kconfig diff: '+json.dumps(changes,sort_keys=True))
 expected=normal_dts(rd.decode())
 expected,n=re.subn(r'(?m)^\t\tgamepad: gamepad \{\n.*?^\t\t\};\n','',expected,flags=re.S)
 require(n==1,'fixture must contain exactly one gamepad node')
 media=['&none']*42
 for p in (35,40,41): media[p]='&to 0x0'
 replacement='\t\tmedia_layer {\n\t\t\tbindings = < '+' '.join(media)+' >;\n\t\t};'
 expected,n=re.subn(r'(?m)^\t\tmedia_layer \{\n.*?^\t\t\};',lambda _:replacement,expected,flags=re.S)
 require(n==1,'fixture must contain exactly one MEDIA layer')
 require(normal_dts(dts)==expected,'DTS differs outside removed gamepad node and restored MEDIA')
 return dict(baseline_run=39743108339,baseline_commit='e2c4966136b5e6af5db795c6c46048672758ba30',assigned_values=len(after),config_changes=changes,other_dts_identical=True,media_bindings=media,other_126_bindings_unchanged=True)
def check_elf(image):
 lo=image.address('_bt_gatt_service_static_list_start');hi=image.address('_bt_gatt_service_static_list_end')
 require(hi>lo and (hi-lo)%8==0,'bad GATT iterable')
 services=[];hid=[];handle=1
 for pos in range(lo,hi,8):
  attrs,count=struct.unpack('<II',image.read(pos,8));require(0<count<1024,'bad attr count')
  first=image.attr(attrs);require(first['uuid']==0x2800,'nonprimary service')
  uuid=image.uuid16(first['user']);services.append(dict(address=hex(pos),uuid16=uuid,attributes=count,first_handle=handle,last_handle=handle+count-1))
  if uuid==0x1812:hid.append((pos,attrs,count,handle))
  handle+=count
 require(len(hid)==1,'must register exactly ONE keyboard/mouse HIDS')
 require(hid[0][0]==image.address('hog_svc') and hid[0][2:]==(22,28),'stock HOG layout/handles changed')
 require(handle==54,'extra or missing GATT attributes')
 require(not any('gamepad' in n.lower() for n in image.symbols),'gamepad symbol survived linking')
 stock=[image.attr(hid[0][1]+i*20) for i in range(22)]
 require([a['uuid'] for a in stock]==STOCK_UUIDS,'stock attribute UUIDs changed')
 def callback(address,name):require(address and address&~1==image.address(name)&~1,'wrong callback '+name)
 for i,name in {2:'read_hids_info',4:'read_hids_report_map',6:'read_hids_input_report',10:'read_hids_consumer_input_report',14:'read_hids_mouse_input_report'}.items():
  callback(stock[i]['read'],name);require(stock[i]['perm']==(1 if i==2 else 4),'stock read permission changed')
 for i,name in [(18,'write_hids_leds_report'),(21,'write_ctrl_point')]:callback(stock[i]['write'],name)
 for i,ref in [(8,(1,1)),(12,(2,1)),(16,(3,1)),(19,(1,2))]:
  require(image.read(stock[i]['user'],2)==bytes(ref),'stock report reference changed')
  callback(stock[i]['read'],'read_hids_report_ref');require(stock[i]['perm']==4 and not stock[i]['write'],'stock Ref access changed')
 for i in (7,11,15):
  callback(stock[i]['read'],'bt_gatt_attr_read_ccc');callback(stock[i]['write'],'bt_gatt_attr_write_ccc')
  require(stock[i]['perm']==12,'stock CCC permission changed')
  changed,write,match=struct.unpack('<III',image.read(stock[i]['user']+52,12))
  callback(changed,'input_ccc_changed');require(write==match==0,'unexpected CCC override')
 for i,properties in [(5,0x12),(9,0x12),(13,0x12),(17,0x0e),(20,0x04)]:require(image.read(stock[i]['user']+6,1)==bytes([properties]),'stock characteristic properties changed')
 callback(stock[0]['read'],'bt_gatt_attr_read_service');require(stock[0]['perm']==1 and not stock[0]['write'],'stock service changed')
 for attr in stock:
  require(attr['handle']==0,'explicit static handle')
  if attr['uuid']==0x2803:
   callback(attr['read'],'bt_gatt_attr_read_chrc');require(attr['perm']==1 and not attr['write'],'declaration changed')
 require(stock[18]['perm']==12 and not stock[18]['read'],'stock LED access changed')
 require(stock[21]['perm']==2 and not stock[21]['read'],'stock CP access changed')
 studio=[x for x in services if x['first_handle']==50]
 require(len(studio)==1 and studio[0]['last_handle']==53 and int(studio[0]['address'],16)==image.address('rpc_interface'),'Studio registration changed')
 attrs,count=struct.unpack('<II',image.read(image.address('rpc_interface'),8));require(count==4,'Studio count changed')
 fields=[image.attr(attrs+i*20) for i in range(count)]
 require(image.read(fields[0]['user'],17)==b'\x02'+bytes.fromhex('2a48c2b1cfc567c90761960100000000'),'Studio UUID changed')
 callback(fields[2]['read'],'read_rpc_resp');callback(fields[2]['write'],'write_rpc_req')
 require(fields[2]['perm']==fields[3]['perm']==12,'Studio permissions changed')
 maps=image.symbols.get('zmk_hid_report_desc',[]);require(bool(maps),'missing stock map')
 for item in maps:require(item['st_size']==196 and hashlib.sha256(image.read(item['st_value'],196)).hexdigest()==STOCK_SHA,'stock 196-byte Map changed')
 for name in ['zmk_hog_send_keyboard_report','zmk_hog_send_consumer_report','zmk_hog_send_mouse_report']:image.symbol(name)
 return dict(services=services,hids_count=1,stock_map_bytes=196,stock_map_sha256=STOCK_SHA,stock_handles=[28,49],studio_handles=[50,53],gamepad_symbols=0,device_tested=False)
def check_commands(entries,stage):
 required={'hog.c':['trackpad_hog_snapshot.h'],'usb.c':['trackpad_usb_epoch.h'],'usb_hid.c':['trackpad_usb_snapshot.h'],'hid.c':['trackpad_hid_owners.h'],'a320_0x57.c':None,'trackpad_microtap_runtime.c':None,'trackpad_tap_output.c':None}
 found={}
 for e in entries:
  tokens=e.get('arguments') or shlex.split(e['command']);text=' '.join(tokens)
  require('gamepad' not in text.lower(),'gamepad compile unit/prefix remains')
  require(not any(x in text for x in ['pure_hog_guard','composite_ble','BBP9981_PURE_BLE','BBP9981_COMPOSITE_DEVICE']),'experimental prefix remains')
  name=Path(e['file']).name
  if name not in required:continue
  require(name not in found,'duplicate production TU '+name)
  source=Path(e['file']);source=source if source.is_absolute() else Path(e['directory'])/source
  suffix=('zmk/app/src/'+name) if name in ('hog.c','usb.c','usb_hid.c','hid.c') else 'config/boards/arm/bbp9981/custom_driver/'+name
  require(source.resolve()==(stage/suffix).resolve(),'wrong compiled source '+name)
  prefixes=[Path(tokens[i+1]).name for i,t in enumerate(tokens[:-1]) if t=='-include']
  if required[name] is not None:require(prefixes==required[name],'stock prefixes changed '+name+':'+str(prefixes))
  found[name]=str(source)
 require(set(found)==set(required),'missing actual production TU')
 return found
def verify_source(commit,stage):
 names=subprocess.check_output(['git','ls-tree','-r','--name-only','-z',commit,'config'],cwd=ROOT).decode().split('\0');names={n for n in names if n}
 actual={p.relative_to(stage).as_posix() for p in (stage/'config').rglob('*') if p.is_file()}
 require(len(names)==73 and actual==names,'wrong config path inventory')
 for name in names:require((stage/name).read_bytes()==subprocess.check_output(['git','show',commit+':'+name],cwd=ROOT),'source drift '+name)
 return len(names)
def main():
 ap=argparse.ArgumentParser(description=__doc__)
 for k in ('build','stage','out'):ap.add_argument('--'+k,type=Path,required=True)
 ap.add_argument('--commit',required=True);ap.add_argument('--run-id',type=int,required=True);a=ap.parse_args()
 require(bool(re.fullmatch('[0-9a-f]{40}',a.commit)) and a.run_id>0,'invalid provenance')
 source_count=verify_source(a.commit,a.stage);z=a.build/'zephyr';config=(z/'.config').read_text()
 hardware=check_hardware(config,(z/'zephyr.dts').read_text())
 from elftools.elf.elffile import ELFFile
 with (z/'zmk.elf').open('rb') as stream:image=Image(ELFFile(stream))
 result=dict(version='v2.3.4-daily',functional_source='bba36b607e0f90401f1840b6a2f897d347f5df52',source_commit=a.commit,source_run=a.run_id,source_files_verified=source_count,device_tested=False,registered=check_elf(image),pnp=check_pnp(image,config,1),uf2=check_uf2_matches(image,(z/'zmk.uf2').read_bytes()),hardware=hardware,compile_units=check_commands(json.loads((a.build/'compile_commands.json').read_text()),a.stage),limitation='Software/ARM only. No physical BLE acceptance. GATT database hash changes after removal; no pairing/settings erase performed.')
 a.out.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n');print(json.dumps(result,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
