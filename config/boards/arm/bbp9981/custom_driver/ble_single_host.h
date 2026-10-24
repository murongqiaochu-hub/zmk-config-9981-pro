/* SPDX-License-Identifier: MIT
 * Optional BLE HID-host connection policy, not a link-layer admission filter.
 *
 * A profile change requests disconnection of non-current inbound HID hosts;
 * every later connection/identity/security event reconciles the latest target.
 * One provisional unknown peer may pair in an empty profile; mapped inactive
 * peers may not. An occupied profile gives an unresolved, unmapped RPA a
 * bounded grace period to resolve before rejecting it. This is not iPad
 * hardware validation.
 *
 * No bond/settings deletion, controller accept list, directed advertising,
 * advertising-state access, HID report changes, or USB policy is performed.
 * Central-role connections and split-peripheral builds remain stock.
 *
 * Disconnection is asynchronous. Short connections, repeated remote attempts,
 * already queued reports and host-UI delays remain possible. A disconnect
 * already submitted cannot be recalled by quickly switching back. Workqueue
 * starvation or repeated stack errors prevent a wall-clock completion bound;
 * retries stop after MAX_ATTEMPTS until a new relevant event rearms them.
 * Pairing interrupted by a profile selection is conservatively rejected; its
 * stock completion is not delegated and its bond is not automatically erased.
 * SMP cancellation/failure releases provisional admission and requests a
 * managed-host disconnect; it never automatically removes that peer's bond.
 * The pairing guard and coherent RAM snapshots rely on the pinned single-core
 * SDK's thread-only selection/nonblocking pre-save prefix, not ISR selection
 * or unknown future SDK behavior. See the completion guard's source contract.
 */
#pragma once

#define BBP_BLE_SINGLE_HOST_RETRY_MS 50U
#define BBP_BLE_SINGLE_HOST_MAX_ATTEMPTS 3U
#define BBP_BLE_SINGLE_HOST_RPA_GRACE_MS 1000U

/* Nonblocking. All work is statically initialized; no startup order hook is
 * needed. OFF, non-BLE and split-peripheral builds provide a no-op definition.
 */
void bbp_ble_single_host_reconcile(void);

/* Used ONLY by the source-specific ble_single_host_prefix.h in pinned ble.c.
 * The prefix must not affect this implementation or another SDK source.
 */
struct bt_conn_auth_cb;
struct bt_conn_auth_info_cb;
int bbp_ble_single_host_auth_cb_register(const struct bt_conn_auth_cb *cb);
int bbp_ble_single_host_auth_info_cb_register(struct bt_conn_auth_info_cb *cb);
