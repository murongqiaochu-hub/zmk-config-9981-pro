/* Used ABI from hal_nordic 884c4d nrfx/drivers/include/nrfx_usbd.h.
 * Copyright (c) 2016-2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: BSD-3-Clause
 * This mock models the busy contract, NOT nRF registers/IRQ/DMA hardware.
 */
#pragma once
#include <stdbool.h>
typedef enum {
    NRFX_USBD_EPOUT0 = 0x00, NRFX_USBD_EPOUT1 = 0x01, NRFX_USBD_EPOUT2 = 0x02,
    NRFX_USBD_EPOUT3 = 0x03, NRFX_USBD_EPOUT4 = 0x04, NRFX_USBD_EPOUT5 = 0x05,
    NRFX_USBD_EPOUT6 = 0x06, NRFX_USBD_EPOUT7 = 0x07, NRFX_USBD_EPOUT8 = 0x08,
    NRFX_USBD_EPIN0 = 0x80, NRFX_USBD_EPIN1 = 0x81, NRFX_USBD_EPIN2 = 0x82,
    NRFX_USBD_EPIN3 = 0x83, NRFX_USBD_EPIN4 = 0x84, NRFX_USBD_EPIN5 = 0x85,
    NRFX_USBD_EPIN6 = 0x86, NRFX_USBD_EPIN7 = 0x87, NRFX_USBD_EPIN8 = 0x88,
} nrfx_usbd_ep_t;
bool nrfx_usbd_ep_is_busy(nrfx_usbd_ep_t ep);
