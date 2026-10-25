/* Status enum/order and callback signature from Zephyr v3.5.0+zmk-fixes.
 * Only the used host API surface is mocked, not USB protocol execution. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>
enum usb_dc_status_code {
    USB_DC_ERROR, USB_DC_RESET, USB_DC_CONNECTED, USB_DC_CONFIGURED,
    USB_DC_DISCONNECTED, USB_DC_SUSPEND, USB_DC_RESUME, USB_DC_INTERFACE,
    USB_DC_SET_HALT, USB_DC_CLEAR_HALT, USB_DC_SOF, USB_DC_UNKNOWN,
};
typedef void (*usb_dc_status_callback)(enum usb_dc_status_code, const uint8_t *);
/* Full eight-byte wire layout; bitfield RequestType aliases are not used. */
struct usb_setup_packet {
    uint8_t bmRequestType, bRequest;
    uint16_t wValue, wIndex, wLength;
} __attribute__((packed));
#define USB_DESC_INTERFACE 0x04
#define USB_BCC_HID 0x03
/* Exact public interface-descriptor layout from usb_ch9.h. */
struct usb_if_descriptor {
    uint8_t bLength, bDescriptorType, bInterfaceNumber, bAlternateSetting;
    uint8_t bNumEndpoints, bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol, iInterface;
} __attribute__((packed));
struct usb_desc_header { uint8_t bLength, bDescriptorType; } __attribute__((packed));
enum usb_dc_ep_cb_status_code { USB_DC_EP_SETUP, USB_DC_EP_DATA_OUT, USB_DC_EP_DATA_IN };
typedef void (*usb_ep_callback)(uint8_t, enum usb_dc_ep_cb_status_code);
typedef int (*usb_request_handler)(struct usb_setup_packet *, int32_t *, uint8_t **);
typedef void (*usb_interface_config)(struct usb_desc_header *, uint8_t);
struct usb_ep_cfg_data { usb_ep_callback ep_cb; uint8_t ep_addr; };
struct usb_interface_cfg_data {
    usb_request_handler class_handler, vendor_handler, custom_handler;
};
/* Exact public usb_cfg_data layout from the fixed usb_device.h. */
struct usb_cfg_data {
    const uint8_t *usb_device_description;
    void *interface_descriptor;
    usb_interface_config interface_config;
    void (*cb_usb_status)(struct usb_cfg_data *, enum usb_dc_status_code, const uint8_t *);
    struct usb_interface_cfg_data interface;
    uint8_t num_endpoints;
    struct usb_ep_cfg_data *endpoint;
};
int usb_enable(usb_dc_status_callback callback);
int usb_wakeup_request(void);
