/* Endpoint shape/enums/API mirrored from ZMK v0.3.0 endpoints_types.h/endpoints.h. */
#pragma once
#include <stdbool.h>
enum zmk_transport { ZMK_TRANSPORT_USB, ZMK_TRANSPORT_BLE };
struct zmk_transport_usb_data {};
struct zmk_transport_ble_data { int profile_index; };
struct zmk_endpoint_instance {
    enum zmk_transport transport;
    union {
        struct zmk_transport_usb_data usb;
        struct zmk_transport_ble_data ble;
    };
};
bool zmk_endpoint_instance_eq(struct zmk_endpoint_instance a, struct zmk_endpoint_instance b);
struct zmk_endpoint_instance zmk_endpoints_selected(void);
