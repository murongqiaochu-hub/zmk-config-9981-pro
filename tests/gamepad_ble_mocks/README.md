# BLE gamepad host-test boundary and fixture provenance

This directory belongs only to the new BLE adapter test. It does not replace or modify any existing SDK checkout, existing fixture, firmware build configuration, device or pairing state.

## Pinned evidence

- Complete HOG translation-unit snapshot: ZMK `v0.3.0`, commit `edf5c0814fd3ea202e43aad2d68fd32e882a518c`, [official source](https://raw.githubusercontent.com/zmkfirmware/zmk/edf5c0814fd3ea202e43aad2d68fd32e882a518c/app/src/hog.c). The local [read-only fixture](</vol1/1000/projects/output/1020/085049_9981ProXbox布局手柄/01_固件源码/tests/gamepad_ble_mocks/sdk_v0_3_0_hog.c>) must stay identical to upstream. Never patch it to make a test pass.
- GATT macro evidence: Zephyr commit `dacab4875df72109b96cc8977547a0dc04875bcd`, [official header](https://raw.githubusercontent.com/zmkfirmware/zephyr/dacab4875df72109b96cc8977547a0dc04875bcd/include/zephyr/bluetooth/gatt.h). The [dedicated GATT mock](</vol1/1000/projects/output/1020/085049_9981ProXbox布局手柄/01_固件源码/tests/gamepad_ble_mocks/zephyr/bluetooth/gatt.h>) faithfully expands service, primary service, characteristic declaration+value, CCC, attribute and descriptor macros. Only linker iterable-section placement and UUID storage representation are simplified.
- The [HID mock](</vol1/1000/projects/output/1020/085049_9981ProXbox布局手柄/01_固件源码/tests/gamepad_ble_mocks/zmk/hid.h>) deliberately contains a short representative immutable IDs1–3 descriptor, not the complete production descriptor. Offset/prefix/suffix identity tests therefore validate the adapter's slicing algorithm, not a complete production HID parse or OS enumeration.

## Run from the source-tree root

```sh
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -Itests/gamepad_ble_mocks tests/test_gamepad_ble.c -o /tmp/test_gamepad_ble
/tmp/test_gamepad_ble
```

Also compile/run with each of these additional flag sets:

```text
-DBBP_GAMEPAD_TEST_WITH_SNAPSHOT=1
-DCONFIG_ZMK_POINTING_SMOOTH_SCROLLING=1
-DBBP_GAMEPAD_TEST_WITH_SNAPSHOT=1 -DCONFIG_ZMK_POINTING_SMOOTH_SCROLLING=1
-DCONFIG_ZMK_POINTING=0 -DCONFIG_ZMK_HID_INDICATORS=0
```

The upstream HOG source has unused callback parameters, hence the narrowly explained `-Wno-unused-parameter`; no other warnings are suppressed. The default test enables pointing and HID indicators, matching the current board configuration. Optional smooth-scroll features prove that gamepad notification selects the final **value** attribute rather than assuming a fixed total count. The snapshot variants include the existing unmodified mouse snapshot preinclude **before** the gamepad extension, as production integration requires.

## Validation status (2026-10-20)

The Xbox-layout standard HID change expands the body to nine bytes; it is **not XInput**. The parent agent has actually compiled and executed the default build plus all four flag sets above (five HOG configurations), together with USB BOOT0/1 × ten modes: all **PASS** under `-Wall -Wextra -Werror`, UBSan and `-fno-sanitize-recover=all`. The parent collected job `bash-7ff9ae3f124f47f6` with exit code0. This transport sub-session has no shell/exec tool; execution evidence was supplied by the parent, not independently run here. The pinned HOG fixture and all other SDK fixtures remain unmodified; the withdrawn seven-byte GBA candidate's older passing results are not evidence for this ABI.

Those initial results covered transport only. Subsequent integration is now complete: the parent actually passed the runtime coalescing/quiet, real A320/micro ownership, asynchronous tap bridge and [full host regression](<../run_host_tests.py>) for functional source `58b5f3b` (collected log, exit0). An actual ARM cloud build and verified UF2 archive have now additionally completed; see the [current delivery record](<../../firmware-delivery/XBOX_CURRENT.md>). Real enumeration and hardware/mobile-app validation remain undone; host models and build success are not hardware evidence.

## Contract coverage

- Actual pinned HOG service declaration and stock keyboard/consumer/mouse sender callbacks, not a handmade attribute count.
- Existing characteristic indices `5`, `9`, `13`, all original declaration/value/CCC/reference groups, LED/feature/control-point ordering, tail declaration/value/independent CCC/reference layout.
- Value `READ_ENCRYPT`; CCC encrypted read/write; Report Reference `4 / INPUT`; report body exactly nine bytes / 72 bits without report ID. Byte order is buttons low/high, hat, signed8 X/Y/Z/Rz, unsigned8 LT/RT; physical shoulder triggers are digital `0/255` (intermediate bytes are raw-wire probes only).
- A lightweight tail-contract helper fails closed on null service/attrs, counts below four, incorrect value callback or missing encryption bit, incorrect reference callback, or non-private reference storage. Synthetic service copies exercise faults without modifying the real const service or the pinned source fixture.
- Every map offset through the end and one beyond, seven read lengths, the stock/new boundary, zero-length reads, `UINT16_MAX`, unchanged stock descriptor prefix and exact appended descriptor suffix.
- Non-map pointer/length mismatches and original HOG reads go through the original helper.
- Every nine-byte body offset0..9 and invalid offset10 / `UINT16_MAX`, zero/short/oversized reads, exact signed-axis byte encoding, both trigger bytes (including isolated offsets7/8) and output canaries. No ID prefix or extra tenth byte is sent.
- Reads from inactive/wrong/reconnected peers, non-BLE routes and changed epochs/peers/routes during the runtime getter return all nine neutral bytes, including hat `8`, centred X/Y/Z/Rz and LT/RT `0`; a hat-only check cannot hide leaked stick/trigger state.
- Directed notifications require a connected, individually subscribed captured connection. Unsubscribed is `-EAGAIN`, not a security-upgrade error.
- Non-neutral checks epoch, selected route/profile and active connection pointer immediately before submission. Trigger-only LT or RT activation (all four sticks/buttons zero, hat8) must still reject stale context, wrong active peer and a USB route; it cannot use the neutral exemption. Stale non-neutral is refused; neutral can release only its old captured peer after route/epoch changes. NULL never broadcasts or reroutes.
- Old-peer neutral still requires that captured peer's own CCC and connected state. Another peer's CCC cannot authorize it; a mocked SDK `-EPERM` security rejection is passed through with no active-peer fallback, retry, security request or work scheduling. This is error propagation coverage, not an ATT encryption implementation test.
- Caller-owned captured references survive send. The active getter's independent temporary references are balanced. Neutral uses no active getter. Notify errors pass through in one attempt with no internal retry/security/work scheduling.
- Optional existing snapshot worker still sends through original mouse index `13` and retains its zero-motion button-snapshot behavior.

These are deterministic host mocks: they do **not** prove radio delivery, host acknowledgement, ATT encryption implementation, real RTOS scheduling, ARM compilation/linking, Android enumeration, pairing migration, or compatibility with an emulator, Moonlight, 盖世 or Droidsteam. No pairing, flashing, ARM build or hardware access is part of this test.
