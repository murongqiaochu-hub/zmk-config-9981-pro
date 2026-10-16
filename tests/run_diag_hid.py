"""Compile real pinned HID + enabled observer hooks, never a target firmware build."""
from pathlib import Path
import os,shlex,subprocess
R=Path(__file__).resolve().parents[1]
cc=shlex.split(os.environ.get('CC','cc'))
cfg={'CONFIG_BBP9981_MENU_DIAG':1,'CONFIG_ZMK_HID_REPORT_TYPE_HKRO':1,'CONFIG_ZMK_HID_REPORT_TYPE_NKRO':0,'CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE':6,'CONFIG_ZMK_HID_CONSUMER_REPORT_SIZE':6,'CONFIG_ZMK_HID_CONSUMER_REPORT_USAGES_BASIC':1,'CONFIG_ZMK_HID_CONSUMER_REPORT_USAGES_FULL':0,'CONFIG_ZMK_HID_KEYBOARD_NKRO_EXTENDED_REPORT':0,'CONFIG_ZMK_POINTING':1,'CONFIG_ZMK_POINTING_SMOOTH_SCROLLING':0,'CONFIG_ZMK_USB_BOOT':1,'CONFIG_ZMK_HID_INDICATORS':1,'CONFIG_ZMK_LOG_LEVEL':4}
exe=R/'.host-test-build/test_diag_hid'
flags=['-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-no-pie']
subprocess.run([*cc,*flags,*[f'-D{k}={v}' for k,v in cfg.items()],'-I',str(R/'tests/hid_snapshot_mocks'),'-include',str(R/'config/boards/arm/bbp9981/custom_driver/trackpad_hid_owners.h'),str(R/'tests/test_diag_hid.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
