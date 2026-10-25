#!/usr/bin/env python3
"""Fail closed if the column experiment or TP gain widens into other config logic."""
from pathlib import Path
import hashlib,json,re,subprocess
ROOT=Path(__file__).resolve().parents[1]
from check_ble_enum_scope import project_config
PROJECTED = project_config()  # Validate exact seam before applying historical oracle.
BASE='3d775f670a6c0a6057c0f4c214cebaca41675807'
DTS='config/boards/arm/bbp9981/bbp9981.dts'
CM='config/boards/arm/bbp9981/CMakeLists.txt'
TP='config/boards/arm/bbp9981/custom_driver/trackpad_led.c'
GUARD='config/boards/arm/bbp9981/custom_driver/matrix_isolation_guard.c'
def old(p):return subprocess.check_output(['git','show',BASE+':'+p],cwd=ROOT)
def clean(s):return re.sub(r'\s+','',re.sub(r'/\*.*?\*/|//[^\n]*','',s,flags=re.S))
paths=subprocess.check_output(['git','ls-tree','-r','--name-only',BASE,'config'],cwd=ROOT,text=True).splitlines()
protected={}
for p in paths:
 if p in (DTS,CM,TP):continue
 a=old(p);b=PROJECTED[p];assert a==b,p;protected[p]=hashlib.sha256(b).hexdigest()
a=old(DTS).decode();b=(ROOT/DTS).read_text()
block=re.search(r'col-gpios\s*(.*?)\s*;',a,re.S).group(0)
assert block.count('GPIO_ACTIVE_HIGH')==10
expected=a.replace(block,block.replace('GPIO_ACTIVE_HIGH','(GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)'))
assert clean(b)==clean(expected),'DTS changed beyond the ten column flags'
a=clean(old(TP).decode());b=clean((ROOT/TP).read_text())
assert a.count('scaled/10000U')==1
assert b==a.replace('scaled/10000U','scaled/(10000U*3U)'),'TP changed beyond final duty gain'
text=(ROOT/GUARD).read_text()
assert all(f'COL({i})' in text for i in range(10)) and all(f'ROW({i})' in text for i in range(6))
assert 'NRF_GPIO_PIN_D0S1 == 4' in text and 'MATRIX_ISO col_hiz_20261017 zephyr=' in text
assert not re.search(r'\bgpio_(?:pin|port)_(?:set|get|configure)\w*\s*\(',text),'Identity guard must not operate GPIO'
cm=PROJECTED[CM].decode();assert 'custom_driver/matrix_isolation_guard.c' in cm and 'BBP_ISO_ZEPHYR_REV=' in cm
assert 'rev-parse HEAD' in cm and 'menu_diag_matrix.h' in cm and 'menu_diag_endpoints.h' in cm
print('PASS narrow scope: ten OPEN_SOURCE flags; TP-only final PWM /3; '+str(len(protected))+' other config files byte-identical; identity guard has no GPIO operations')
