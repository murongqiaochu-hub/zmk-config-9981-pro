#!/usr/bin/env python3
"""Preserve the user-accepted345 column/TP-light base; narrow changes to tap policy."""
from pathlib import Path
import hashlib,re,subprocess
ROOT=Path(__file__).resolve().parents[1]
BASE='7fce2be906f8f54517226bcd229fab6aa9c8e979'
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
core=clean((ROOT/(PREFIX+'trackpad_microtap.h')).read_text())
for name,value in [('MIN_FRAMES',1),('MAX_FRAMES',8),('MIN_CURSOR_DISTANCE_PX',2),('MAX_CURSOR_DISTANCE_PX',50),('QUIET_MS',120),('MAX_CANDIDATE_MS',360),('MAX_MOTION_SPAN_MS',160),('CLICK_INTERVAL_MS',500),('MAX_PATH',8192),('MAX_OFFSET',4096)]:
 assert f'#defineTP_MICROTAP_{name}{value}U' in core,name
assert 'TP_MICROTAP_MIN_PATH' not in core,'Do not use raw path as click threshold'
assert 'tp_pointer_motion(&s->measure_pointer' in core,'Eligibility must use the same scaled cursor pipeline'
assert 'tp_microtap_cursor_distance_in_range(s)' in core,'Require the inferred screen-space 2..50px window'
runtime=(ROOT/(PREFIX+'trackpad_microtap_runtime.c')).read_text()
assert 'MICROTAP_POLICY px=2..50 quiet=120 gap=500 native=600 range=8192/4096 frames=1..8 span=160 age=360 gain=480/400;slow=240/400' in runtime
workflow=(ROOT/'.github/workflows/main.yml').read_text()
branch='codex/tap-window-widen-20261017'
assert workflow.count(f"refs/heads/{branch}")==1 and workflow.count(f"HEAD:{branch}")==1
assert not re.search(r'\b(?:zmk_hid_mouse_button(?:s)?_(?:press|release)|zmk_behavior_invoke_binding|input_report_key)\s*\(',runtime),'Do not filter/send native mouse edges in tap runtime'
assert len(protected)>=51
print(f'PASS345 successor scope: {len(protected)} base config files byte-identical including column DT/guard, TP PWM/3, keys, Ctrl, memories, pointer, HID/bridges/shoulder; tap core owns the inferred 2..50 screen-pixel window and runtime marker')
