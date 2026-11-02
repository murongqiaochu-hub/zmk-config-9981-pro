#!/usr/bin/env python3
"""Recheck downloaded daily ARM image and negative controls, never flash."""
import argparse,hashlib,json,struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from elf_hid_artifact import Image,require,check_pnp,check_uf2_matches
from verify_daily_keymouse import ROOT,check_elf,check_hardware,check_commands,verify_source

def main():
 ap=argparse.ArgumentParser(description=__doc__)
 for k in ('artifacts','run-metadata','out'):ap.add_argument('--'+k,type=Path,required=True)
 a=ap.parse_args();meta=json.loads(a.run_metadata.read_text());cloud=json.loads((a.artifacts/'daily-validation.json').read_text())
 require(meta['status']=='completed' and meta['conclusion']=='success','run not successful')
 require(meta['headBranch']=='codex/keymouse-daily-20261102','wrong branch')
 require(meta['headSha']==cloud['source_commit'] and meta['databaseId']==cloud['source_run'],'run provenance mismatch')
 require(cloud['version']=='v2.3.4-daily' and cloud['device_tested'] is False,'wrong version/acceptance claim')
 count=verify_source(meta['headSha'],ROOT)
 z=a.artifacts/'zephyr';config=(z/'.config').read_text();entries=json.loads((a.artifacts/'compile_commands.json').read_text())
 hog=[e for e in entries if Path(e['file']).name=='hog.c'];require(len(hog)==1,'wrong hog TU count')
 source=Path(hog[0]['file']);source=source if source.is_absolute() else Path(hog[0]['directory'])/source
 require(source.as_posix().endswith('/zmk/app/src/hog.c'),'unexpected compiled source path')
 with (z/'zmk.elf').open('rb') as f:image=Image(ELFFile(f))
 linked=check_elf(image);pnp=check_pnp(image,config,1);uf2=check_uf2_matches(image,(z/'zmk.uf2').read_bytes());hardware=check_hardware(config,(z/'zephyr.dts').read_text());commands=check_commands(entries,source.parents[3])
 for key,val in [('registered',linked),('pnp',pnp),('uf2',uf2),('hardware',hardware),('compile_units',commands)]:require(cloud[key]==val,'cloud/download mismatch '+key)
 attrs,_=struct.unpack('<II',image.read(image.address('hog_svc'),8));studio,_=struct.unpack('<II',image.read(image.address('rpc_interface'),8))
 descriptor=image.symbols['zmk_hid_report_desc'][0]['st_value']
 cases=[('stock-info-permission',attrs+2*20+18,struct.pack('<H',4),'stock read permission changed'),('ccc-read-callback',attrs+7*20+4,b'\0'*4,'wrong callback bt_gatt_attr_read_ccc'),('report-ref-write',attrs+8*20+8,struct.pack('<I',image.address('write_ctrl_point')),'stock Ref access changed'),('ccc-invalidation-callback',image.attr(attrs+7*20)['user']+52,b'\0'*4,'wrong callback input_ccc_changed'),('studio-uuid',image.attr(studio)['user']+1,b'\0','Studio UUID changed'),('stock-report-map',descriptor,b'\xff','stock 196-byte Map changed')]
 class Mutated:
  def __init__(self,start,data):self.start=start;self.data=data
  def __getattr__(self,k):return getattr(image,k)
  def read(self,start,size):
   out=bytearray(image.read(start,size));lo=max(start,self.start);hi=min(start+size,self.start+len(self.data))
   if hi>lo:out[lo-start:hi-start]=self.data[lo-self.start:hi-self.start]
   return bytes(out)
  def attr(self,address):return Image.attr(self,address)
  def uuid16(self,address):return Image.uuid16(self,address)
 rejected=[]
 for name,start,data,error in cases:
  try:check_elf(Mutated(start,data))
  except ValueError as e:require(str(e)==error,'negative rejected for wrong reason '+name+': '+str(e));rejected.append(name)
  else:raise ValueError('corrupted artifact accepted '+name)
 files={p.relative_to(a.artifacts).as_posix():dict(bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in sorted(a.artifacts.rglob('*')) if p.is_file()}
 result=dict(version='v2.3.4-daily',source_commit=meta['headSha'],source_run=meta['databaseId'],run_url=meta['url'],source_files_verified=count,registered=linked,pnp=pnp,uf2=uf2,hardware=hardware,compile_units=commands,actual_elf_negative_controls=rejected,artifacts=files,device_tested=False,limitations='Software/ARM only; no physical BLE acceptance, no pairing/settings erase, no flashing.')
 a.out.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
 print('PASS downloaded pure keyboard/mouse ARM/UF2; 73 config files; 1168 Kconfig values; complete DTS; 6 real-ELF negative controls; NOT device tested')
 print(json.dumps(uf2,sort_keys=True))
if __name__=='__main__':main()
