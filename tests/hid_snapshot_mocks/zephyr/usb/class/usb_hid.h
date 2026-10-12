/* Standard short-item descriptor encodings needed by the official hid.h.
 * This is not a USB transport mock and does not redefine any report structure. */
#pragma once
#define HID_ITEM_TYPE_MAIN 0
#define HID_ITEM_TYPE_GLOBAL 1
#define HID_ITEM_TYPE_LOCAL 2
#define HID_ITEM_TAG_PUSH 0xA
#define HID_ITEM_TAG_POP 0xB
#define HID_ITEM_TAG_PHYSICAL_MIN 0x3
#define HID_ITEM_TAG_PHYSICAL_MAX 0x4
#define HID_ITEM_TAG_USAGE 0x0
#define HID_ITEM(tag, type, size) (((tag) << 4) | ((type) << 2) | (size))
#define HID_USAGE_GEN_DESKTOP 0x01
#define HID_COLLECTION_PHYSICAL 0x00
#define HID_COLLECTION_APPLICATION 0x01
#define HID_COLLECTION_LOGICAL 0x02
#define HID_USAGE_PAGE(a) HID_ITEM(0x0, HID_ITEM_TYPE_GLOBAL, 1), (a)
#define HID_USAGE(a) HID_ITEM(HID_ITEM_TAG_USAGE, HID_ITEM_TYPE_LOCAL, 1), (a)
#define HID_COLLECTION(a) HID_ITEM(0xA, HID_ITEM_TYPE_MAIN, 1), (a)
#define HID_END_COLLECTION HID_ITEM(0xC, HID_ITEM_TYPE_MAIN, 0)
#define HID_REPORT_ID(a) HID_ITEM(0x8, HID_ITEM_TYPE_GLOBAL, 1), (a)
#define HID_USAGE_MIN8(a) HID_ITEM(0x1, HID_ITEM_TYPE_LOCAL, 1), (a)
#define HID_USAGE_MAX8(a) HID_ITEM(0x2, HID_ITEM_TYPE_LOCAL, 1), (a)
#define HID_USAGE_MAX16(a, b) HID_ITEM(0x2, HID_ITEM_TYPE_LOCAL, 2), (a), (b)
#define HID_LOGICAL_MIN8(a) HID_ITEM(0x1, HID_ITEM_TYPE_GLOBAL, 1), (a)
#define HID_LOGICAL_MAX8(a) HID_ITEM(0x2, HID_ITEM_TYPE_GLOBAL, 1), (a)
#define HID_LOGICAL_MIN16(a, b) HID_ITEM(0x1, HID_ITEM_TYPE_GLOBAL, 2), (a), (b)
#define HID_LOGICAL_MAX16(a, b) HID_ITEM(0x2, HID_ITEM_TYPE_GLOBAL, 2), (a), (b)
#define HID_REPORT_SIZE(a) HID_ITEM(0x7, HID_ITEM_TYPE_GLOBAL, 1), (a)
#define HID_REPORT_COUNT(a) HID_ITEM(0x9, HID_ITEM_TYPE_GLOBAL, 1), (a)
#define HID_INPUT(a) HID_ITEM(0x8, HID_ITEM_TYPE_MAIN, 1), (a)
#define HID_OUTPUT(a) HID_ITEM(0x9, HID_ITEM_TYPE_MAIN, 1), (a)
#define HID_FEATURE(a) HID_ITEM(0xB, HID_ITEM_TYPE_MAIN, 1), (a)
