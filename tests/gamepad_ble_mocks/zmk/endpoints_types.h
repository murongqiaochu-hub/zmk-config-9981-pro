/* SPDX-License-Identifier: MIT -- ZMK v0.3.0 public endpoint ABI. */
#pragma once
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
