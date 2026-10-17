#!/usr/bin/env python3
"""Preserve the user-accepted345 column/TP-light base; narrow changes to tap policy."""
from pathlib import Path
import hashlib,re,subprocess
ROOT=Path(__file__).resolve().parents[1]
BASE='7e485368865409f05c42c7ae5bd955f1f9ccb91c'
PREFIX='config/boards/arm/bbp9981/custom_driver/'
ALLOWED={PREFIX+n for n in ['trackpad_microtap.h','trackpad_microtap_runtime.c','trackpad_microtap_runtime.h']}
paths=subprocess.check_output(['git','ls-tree','-r','--name-only',BASE,'config'],cwd=ROOT,text=True).splitlines()
def old(p):return subprocess.check_output(['git','show',BASE+':'+p],cwd=ROOT)
def clean(s):return re.sub(r'\s+','',re.sub(r'/\*.*?\*/|//[^\n]*','',s,flags=re.S))
protected={}
for p in paths:
 a=old(p);b=(ROOT/p).read_bytes()
 if p not in ALLOWED:assert a==b,p
 if a==b:protected[p]=hashlib.sha256(b).hexdigest()
a=clean(old(PREFIX+'trackpad_microtap.h').decode());b=clean((ROOT/(PREFIX+'trackpad_microtap.h')).read_text())
for name,before,after in [('QUIET_MS',60,120),('MIN_PATH',3,6),('MAX_CANDIDATE_MS',120,180),('CLICK_INTERVAL_MS',200,350),('MAX_PATH',512,1024),('MAX_OFFSET',256,512)]:
 token=f'#defineTP_MICROTAP_{name}{before}U';assert a.count(token)==1,token;a=a.replace(token,f'#defineTP_MICROTAP_{name}{after}U')
assert a==b,'Pure core changed beyond the six declared parameters'
runtime=(ROOT/(PREFIX+'trackpad_microtap_runtime.c')).read_text()
assert 'MICROTAP_POLICY floor=6 quiet=120 gap=350 native=600 range=1024/512' in runtime
assert not re.search(r'\b(?:zmk_hid_mouse_button(?:s)?_(?:press|release)|zmk_behavior_invoke_binding|input_report_key)\s*\(',runtime),'Do not filter/send native mouse edges in tap runtime'
assert len(protected)>=51
print(f'PASS345 successor scope: {len(protected)} base config files byte-identical including column DT/guard, TP PWM/3, keys, Ctrl, memories, pointer, HID/bridges/shoulder; only six core constants and runtime auto guard may differ')
