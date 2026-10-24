/* SPDX-License-Identifier: MIT
 * Reconcile inbound HID-host connections without changing stock advertising.
 */
#include "ble_single_host.h"
#include <zephyr/kernel.h>

#if IS_ENABLED(CONFIG_BBP9981_BLE_SINGLE_HOST) && IS_ENABLED(CONFIG_ZMK_BLE) && \
    !(IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/logging/log.h>
#include <zmk/ble.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>

BUILD_ASSERT(IS_ENABLED(CONFIG_BT_SMP_APP_PAIRING_ACCEPT),
             "Single-host pairing guard requires BT_SMP_APP_PAIRING_ACCEPT");
LOG_MODULE_REGISTER(bbp_ble_single_host, CONFIG_ZMK_LOG_LEVEL);

struct policy_target {
    int index;
    bt_addr_le_t peer;
};

/* Tokens are compared, never dereferenced outside a stack callback/foreach.
 * foreach owns its temporary bt_conn reference. No connection reference is
 * retained by this service, so a disconnected object is not kept allocated.
 * A connected/disconnected callback advances lifetime, including pool reuse
 * at the very same address/index. revision also invalidates in-flight work
 * after identity/security/pairing evidence changes.
 */
struct policy_slot {
    const struct bt_conn *token;
    uint32_t lifetime;
    uint32_t revision;
    uint32_t retry_epoch;
    int admission_profile;
    uint32_t admission_selection;
    int64_t born_at;
    int64_t retry_at;
    uint8_t failures;
    bool pairing_pending;
    bool pairing_rejected;
    int pairing_profile;
    uint32_t pairing_selection;
};

static struct k_spinlock policy_lock;
static struct policy_slot slots[CONFIG_BT_MAX_CONN];
static uint32_t wake_epoch;
static uint32_t retry_epoch;
static uint32_t selection_epoch;
static int observed_profile = -1;
static struct {
    bool valid;
    uint8_t index;
    uint32_t lifetime;
    int profile;
} open_owner;

static void policy_worker(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(policy_work, policy_worker);

/* Only RAM bookkeeping and the nonblocking work API run under policy_lock. */
static void wake_locked(void) {
    ++wake_epoch;
    (void)k_work_reschedule(&policy_work, K_NO_WAIT);
}

static void observe_selection_locked(int index) {
    if (index >= 0 && index < ZMK_BLE_PROFILE_COUNT && observed_profile != index) {
        observed_profile = index;
        ++selection_epoch;
    }
}

static void begin_lifetime_locked(struct policy_slot *slot, const struct bt_conn *conn,
                                  int index, int64_t now) {
    uint32_t lifetime = slot->lifetime + 1U;
    uint32_t revision = slot->revision + 1U;
    *slot = (struct policy_slot){
        .token = conn, .lifetime = lifetime, .revision = revision,
        .retry_epoch = retry_epoch, .admission_profile = index,
        .admission_selection = selection_epoch, .born_at = now,
    };
}

static bool target_snapshot(struct policy_target *target) {
    /* Pinned single-core nRF target: these two ZMK getters only read RAM, and
     * profile/address writers run in threads. Prevent a same-index peer memcpy
     * from preempting this short snapshot. No Bluetooth/connection/settings
     * API, log or wait occurs here. ISR profile writers or an SMP port require
     * re-review. Nesting inside the completion guard's scheduler lock is valid.
     */
    k_sched_lock();
    int index = zmk_ble_active_profile_index();
    bool valid = index >= 0 && index < ZMK_BLE_PROFILE_COUNT;
    if (valid) {
        const bt_addr_le_t *addr = zmk_ble_profile_address((uint8_t)index);
        valid = addr != NULL;
        if (valid) {
            target->index = index;
            target->peer = *addr;
            valid = zmk_ble_active_profile_index() == index;
        }
    }
    k_sched_unlock();
    return valid;
}

static bool target_equal(const struct policy_target *a, const struct policy_target *b) {
    return a->index == b->index && bt_addr_le_cmp(&a->peer, &b->peer) == 0;
}

static bool target_open(const struct policy_target *target) {
    return bt_addr_le_cmp(&target->peer, BT_ADDR_LE_ANY) == 0;
}

static bool mapped_peer(const bt_addr_le_t *peer) {
    for (uint8_t i = 0; i < ZMK_BLE_PROFILE_COUNT; ++i) {
        const bt_addr_le_t *addr = zmk_ble_profile_address(i);
        if (addr != NULL && bt_addr_le_cmp(addr, BT_ADDR_LE_ANY) != 0 &&
            bt_addr_le_cmp(addr, peer) == 0) { return true; }
    }
    return false;
}

static bool host_connection(const struct bt_conn_info *info) {
    return info->type == BT_CONN_TYPE_LE && info->id == BT_ID_DEFAULT &&
           info->role == BT_CONN_ROLE_PERIPHERAL;
}

static bool unresolved_rpa(const bt_addr_le_t *peer) {
    /* Use the connection's identity destination, not its on-air remote address.
     * The stack normally resolves bonded RPAs before connected is delivered.
     */
    return peer->type == BT_ADDR_LE_RANDOM && (peer->a.val[5] & 0xc0U) == 0x40U;
}

void bbp_ble_single_host_reconcile(void) {
    k_sched_lock();
    int index = zmk_ble_active_profile_index();
    k_spinlock_key_t key = k_spin_lock(&policy_lock);
    observe_selection_locked(index);
    ++retry_epoch;
    wake_locked();
    k_spin_unlock(&policy_lock, key);
    k_sched_unlock();
}

static void policy_connected(struct bt_conn *conn, uint8_t err) {
    if (err != 0U) { return; }
    uint8_t i = bt_conn_index(conn);
    if (i >= CONFIG_BT_MAX_CONN) { return; }
    int64_t now = k_uptime_get();
    k_sched_lock();
    int index = zmk_ble_active_profile_index();
    k_spinlock_key_t key = k_spin_lock(&policy_lock);
    observe_selection_locked(index);
    begin_lifetime_locked(&slots[i], conn, index, now);
    wake_locked();
    k_spin_unlock(&policy_lock, key);
    k_sched_unlock();
}

static void policy_disconnected(struct bt_conn *conn, uint8_t reason) {
    (void)reason;
    uint8_t i = bt_conn_index(conn);
    if (i >= CONFIG_BT_MAX_CONN) { return; }
    k_spinlock_key_t key = k_spin_lock(&policy_lock);
    if (slots[i].token == conn) {
        slots[i].token = NULL;
        ++slots[i].lifetime;
        ++slots[i].revision;
        slots[i].pairing_pending = false;
        if (open_owner.valid && open_owner.index == i) { open_owner.valid = false; }
    }
    wake_locked();
    k_spin_unlock(&policy_lock, key);
}

static void connection_evidence(struct bt_conn *conn) {
    uint8_t i = bt_conn_index(conn);
    if (i >= CONFIG_BT_MAX_CONN) { return; }
    k_spinlock_key_t key = k_spin_lock(&policy_lock);
    if (slots[i].token == conn) {
        ++slots[i].revision;
        slots[i].failures = 0U;
        slots[i].retry_at = 0;
    }
    wake_locked();
    k_spin_unlock(&policy_lock, key);
}

static void policy_identity_resolved(struct bt_conn *conn, const bt_addr_le_t *rpa,
                                     const bt_addr_le_t *identity) {
    (void)rpa;
    (void)identity;
    connection_evidence(conn);
}

static void policy_security_changed(struct bt_conn *conn, bt_security_t level,
                                    enum bt_security_err err) {
    (void)level;
    (void)err;
    /* Encryption alone is not proof of the selected identity. */
    connection_evidence(conn);
}

BT_CONN_CB_DEFINE(bbp_ble_single_host) = {
    .connected = policy_connected,
    .disconnected = policy_disconnected,
    .identity_resolved = policy_identity_resolved,
    .security_changed = policy_security_changed,
};

struct policy_sweep {
    uint32_t wake;
    int64_t next_at;
    bool rerun;
};

enum policy_decision { POLICY_ALLOW, POLICY_DROP, POLICY_GRACE, POLICY_STALE };

static void due_at(struct policy_sweep *sweep, int64_t at) {
    if (at < sweep->next_at) { sweep->next_at = at; }
}

static bool same_slot_locked(uint8_t i, const struct policy_slot *snapshot) {
    return slots[i].token == snapshot->token && slots[i].lifetime == snapshot->lifetime &&
           slots[i].revision == snapshot->revision;
}

static void release_open_owner_locked(uint8_t i, uint32_t lifetime) {
    if (open_owner.valid && open_owner.index == i && open_owner.lifetime == lifetime) {
        open_owner.valid = false;
    }
}

static void release_open_owner(uint8_t i, const struct policy_slot *snapshot) {
    k_spinlock_key_t key = k_spin_lock(&policy_lock);
    release_open_owner_locked(i, snapshot->lifetime);
    k_spin_unlock(&policy_lock, key);
}

static bool claim_open_owner_locked(uint8_t i, uint32_t lifetime, int profile) {
    if (open_owner.valid &&
        (open_owner.profile != profile || slots[open_owner.index].token == NULL ||
         slots[open_owner.index].lifetime != open_owner.lifetime)) {
        open_owner.valid = false;
    }
    if (!open_owner.valid) {
        open_owner.valid = true;
        open_owner.index = i;
        open_owner.lifetime = lifetime;
        open_owner.profile = profile;
    }
    return open_owner.index == i && open_owner.lifetime == lifetime;
}

static void operation_done(uint8_t i, const struct policy_slot *snapshot) {
    k_spinlock_key_t key = k_spin_lock(&policy_lock);
    if (same_slot_locked(i, snapshot)) {
        slots[i].failures = 0U;
        slots[i].retry_at = 0;
    }
    k_spin_unlock(&policy_lock, key);
}

static void operation_failed(uint8_t i, const struct policy_slot *snapshot,
                             struct policy_sweep *sweep, int err) {
    bool exhausted = false;
    k_spinlock_key_t key = k_spin_lock(&policy_lock);
    if (same_slot_locked(i, snapshot) && wake_epoch == sweep->wake) {
        if (slots[i].failures < BBP_BLE_SINGLE_HOST_MAX_ATTEMPTS) { ++slots[i].failures; }
        exhausted = slots[i].failures == BBP_BLE_SINGLE_HOST_MAX_ATTEMPTS;
        if (!exhausted) {
            slots[i].retry_at = k_uptime_get() + BBP_BLE_SINGLE_HOST_RETRY_MS;
            due_at(sweep, slots[i].retry_at);
        }
    }
    k_spin_unlock(&policy_lock, key);
    if (exhausted) { LOG_WRN("BLE host policy retries exhausted (%d)", err); }
}

static enum policy_decision decide(uint8_t i, const struct policy_slot *snapshot,
                                   const struct policy_target *target, const bt_addr_le_t *peer,
                                   struct policy_sweep *sweep) {
    if (snapshot->pairing_rejected) {
        release_open_owner(i, snapshot);
        return POLICY_DROP;
    }
    if (!target_open(target)) {
        release_open_owner(i, snapshot);
        /* Compare the active address first: duplicate legacy profile mappings
         * must not make zmk_ble_profile_index's first match reject this host.
         */
        if (bt_addr_le_cmp(&target->peer, peer) == 0) { return POLICY_ALLOW; }
        if (mapped_peer(peer)) { return POLICY_DROP; }
        int64_t until = snapshot->born_at + BBP_BLE_SINGLE_HOST_RPA_GRACE_MS;
        if (unresolved_rpa(peer) && k_uptime_get() < until) {
            due_at(sweep, until);
            return POLICY_GRACE;
        }
        return POLICY_DROP;
    }
    if (mapped_peer(peer)) {
        release_open_owner(i, snapshot);
        return POLICY_DROP;
    }

    k_spinlock_key_t key = k_spin_lock(&policy_lock);
    enum policy_decision result = POLICY_DROP;
    if (!same_slot_locked(i, snapshot) || wake_epoch != sweep->wake ||
        observed_profile != target->index) {
        sweep->rerun = true;
        result = POLICY_STALE;
    } else if (snapshot->admission_profile == target->index &&
               snapshot->admission_selection == selection_epoch) {
        /* Only one provisional unknown host owns an open profile. Keep that
         * owner stable across security events; reconnect/pool reuse changes
         * lifetime. No connection pointer is dereferenced here.
         */
        if (claim_open_owner_locked(i, snapshot->lifetime, target->index)) {
            result = POLICY_ALLOW;
        }
    }
    if (result == POLICY_DROP) { release_open_owner_locked(i, snapshot->lifetime); }
    k_spin_unlock(&policy_lock, key);
    return result;
}

static void reconcile_connection(struct bt_conn *conn, void *data) {
    struct policy_sweep *sweep = data;
    uint8_t i = bt_conn_index(conn);
    if (i >= CONFIG_BT_MAX_CONN) { return; }
    int64_t now = k_uptime_get();
    struct policy_slot snapshot;
    k_sched_lock();
    int index = zmk_ble_active_profile_index();
    k_spinlock_key_t key = k_spin_lock(&policy_lock);
    observe_selection_locked(index);
    if (slots[i].token != conn) { begin_lifetime_locked(&slots[i], conn, index, now); }
    if (slots[i].retry_epoch != retry_epoch) {
        slots[i].retry_epoch = retry_epoch;
        slots[i].failures = 0U;
        slots[i].retry_at = 0;
    }
    snapshot = slots[i];
    k_spin_unlock(&policy_lock, key);
    k_sched_unlock();
    if (snapshot.failures >= BBP_BLE_SINGLE_HOST_MAX_ATTEMPTS) { return; }
    if (snapshot.retry_at > now) { due_at(sweep, snapshot.retry_at); return; }

    /* foreach's borrowed reference remains live throughout this callback. */
    struct bt_conn_info info;
    int err = bt_conn_get_info(conn, &info);
    if (err != 0) { operation_failed(i, &snapshot, sweep, err); return; }
    if (!host_connection(&info) || info.state != BT_CONN_STATE_CONNECTED) {
        release_open_owner(i, &snapshot);
        operation_done(i, &snapshot);
        return;
    }
    struct policy_target target;
    if (!target_snapshot(&target)) { operation_failed(i, &snapshot, sweep, -EAGAIN); return; }
    bt_addr_le_t peer = *bt_conn_get_dst(conn);
    enum policy_decision decision = decide(i, &snapshot, &target, &peer, sweep);
    if (decision != POLICY_DROP) {
        operation_done(i, &snapshot);
        return;
    }

    /* Re-read both identity/role and the latest selected profile immediately
     * before a destructive action. No captured old target is a command.
     */
    err = bt_conn_get_info(conn, &info);
    if (err != 0) { operation_failed(i, &snapshot, sweep, err); return; }
    if (!host_connection(&info) || info.state != BT_CONN_STATE_CONNECTED) {
        release_open_owner(i, &snapshot);
        operation_done(i, &snapshot);
        return;
    }
    if (!target_snapshot(&target)) { operation_failed(i, &snapshot, sweep, -EAGAIN); return; }
    peer = *bt_conn_get_dst(conn);
    decision = decide(i, &snapshot, &target, &peer, sweep);
    if (decision != POLICY_DROP) { operation_done(i, &snapshot); return; }
    struct policy_target confirmed;
    if (!target_snapshot(&confirmed)) {
        operation_failed(i, &snapshot, sweep, -EAGAIN);
        return;
    }
    if (!target_equal(&target, &confirmed)) { sweep->rerun = true; return; }
    key = k_spin_lock(&policy_lock);
    bool current = same_slot_locked(i, &snapshot) && wake_epoch == sweep->wake;
    k_spin_unlock(&policy_lock, key);
    if (!current) { sweep->rerun = true; return; }

    err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    if (err == 0 || err == -ENOTCONN) {
        /* The pinned stack also returns 0 while already disconnecting. This
         * means accepted/already gone, not a peer acknowledgement.
         */
        operation_done(i, &snapshot);
    } else {
        operation_failed(i, &snapshot, sweep, err);
    }
}

static void policy_worker(struct k_work *work) {
    (void)work;
    k_spinlock_key_t key = k_spin_lock(&policy_lock);
    struct policy_sweep sweep = {.wake = wake_epoch, .next_at = INT64_MAX};
    k_spin_unlock(&policy_lock, key);
    bt_conn_foreach(BT_CONN_TYPE_LE, reconcile_connection, &sweep);
    key = k_spin_lock(&policy_lock);
    /* A new event cannot be overwritten by this worker's later retry timer. */
    if (sweep.rerun || sweep.wake != wake_epoch) {
        (void)k_work_reschedule(&policy_work, K_NO_WAIT);
    } else if (sweep.next_at != INT64_MAX) {
        int64_t remaining = sweep.next_at - k_uptime_get();
        (void)k_work_reschedule(&policy_work, remaining > 0 ? K_MSEC(remaining) : K_NO_WAIT);
    }
    k_spin_unlock(&policy_lock, key);
}

static int policy_profile_listener(const zmk_event_t *eh) {
    if (as_zmk_ble_active_profile_changed(eh) != NULL) { bbp_ble_single_host_reconcile(); }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(bbp_ble_single_host, policy_profile_listener);
ZMK_SUBSCRIPTION(bbp_ble_single_host, zmk_ble_active_profile_changed);

/* The only source-prefix interception is authentication registration. Copies
 * preserve every original callback, including passkey entry/cancel. */
static struct bt_conn_auth_cb native_auth;
static struct bt_conn_auth_cb guarded_auth;
static bool auth_registered;
static struct bt_conn_auth_info_cb native_auth_info;
static struct bt_conn_auth_info_cb guarded_auth_info;
static bool auth_info_registered;

static void reject_pairing(struct bt_conn *conn) {
    uint8_t i = bt_conn_index(conn);
    if (i >= CONFIG_BT_MAX_CONN) { return; }
    int64_t now = k_uptime_get();
    k_sched_lock();
    int index = zmk_ble_active_profile_index();
    k_spinlock_key_t key = k_spin_lock(&policy_lock);
    observe_selection_locked(index);
    if (slots[i].token != conn) { begin_lifetime_locked(&slots[i], conn, index, now); }
    release_open_owner_locked(i, slots[i].lifetime);
    slots[i].pairing_pending = false;
    slots[i].pairing_rejected = true;
    slots[i].failures = 0U;
    slots[i].retry_at = 0;
    ++slots[i].revision;
    wake_locked();
    k_spin_unlock(&policy_lock, key);
    k_sched_unlock();
}

static void clear_pairing(struct bt_conn *conn) {
    uint8_t i = bt_conn_index(conn);
    if (i >= CONFIG_BT_MAX_CONN) { return; }
    k_spinlock_key_t key = k_spin_lock(&policy_lock);
    if (slots[i].token == conn) { slots[i].pairing_pending = false; }
    k_spin_unlock(&policy_lock, key);
}

/* Called synchronously by SMP. This bounded profile-array check performs no
 * bond enumeration, work wait, HCI command, settings operation or SDK callback.
 */
static bool pairing_gate(struct bt_conn *conn, bool complete, bool *is_host) {
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) != 0) { return false; }
    *is_host = host_connection(&info);
    if (!*is_host) { return true; }
    if (info.state != BT_CONN_STATE_CONNECTED) { return false; }
    /* Keep a RAM selection snapshot from becoming a stale observation of
     * selection_epoch before the bookkeeping lock is acquired. All operations
     * in this small gate are nonblocking on the pinned SDK; no delegate runs.
     */
    k_sched_lock();
    struct policy_target target;
    if (!target_snapshot(&target)) { k_sched_unlock(); return false; }
    bt_addr_le_t peer = *bt_conn_get_dst(conn);
    bool matches = bt_addr_le_cmp(&target.peer, &peer) == 0;
    bool open = target_open(&target);
    if ((!open &&
         !(IS_ENABLED(CONFIG_BT_SMP_ALLOW_UNAUTH_OVERWRITE) && matches)) ||
        (!matches && mapped_peer(&peer))) { k_sched_unlock(); return false; }
    uint8_t i = bt_conn_index(conn);
    if (i >= CONFIG_BT_MAX_CONN) { k_sched_unlock(); return false; }
    int64_t now = k_uptime_get();
    k_spinlock_key_t key = k_spin_lock(&policy_lock);
    observe_selection_locked(target.index);
    if (slots[i].token != conn) { begin_lifetime_locked(&slots[i], conn, target.index, now); }
    struct policy_slot *slot = &slots[i];
    bool allowed = !slot->pairing_rejected && slot->admission_profile == target.index &&
                   slot->admission_selection == selection_epoch;
    if (complete) {
        allowed = allowed && slot->pairing_pending && slot->pairing_profile == target.index &&
                  slot->pairing_selection == selection_epoch;
    }
    if (allowed && open) {
        /* Pairing may begin before the workqueue gets a turn. Admission must
         * share the worker's one-provisional-host owner, not race two SMPs.
         */
        allowed = claim_open_owner_locked(i, slot->lifetime, target.index);
    }
    if (!allowed) {
        release_open_owner_locked(i, slot->lifetime);
    } else if (!complete) {
        slot->pairing_pending = true;
        slot->pairing_profile = target.index;
        slot->pairing_selection = selection_epoch;
    }
    k_spin_unlock(&policy_lock, key);
    k_sched_unlock();
    return allowed;
}

static enum bt_security_err guarded_pairing_accept(
    struct bt_conn *conn, const struct bt_conn_pairing_feat *const feat) {
    bool is_host = false;
    if (!pairing_gate(conn, false, &is_host)) {
        reject_pairing(conn);
        return BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
    }
    enum bt_security_err result = native_auth.pairing_accept
        ? native_auth.pairing_accept(conn, feat) : BT_SECURITY_ERR_SUCCESS;
    if (result != BT_SECURITY_ERR_SUCCESS || !pairing_gate(conn, true, &is_host)) {
        reject_pairing(conn);
        return result != BT_SECURITY_ERR_SUCCESS ? result : BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
    }
    return result;
}

static void guarded_pairing_complete(struct bt_conn *conn, bool bonded) {
    bool is_host = false;
    /* PINNED SDK CONTRACT: from this final gate through the native complete's
     * set_profile_address(active_profile, dst) argument capture, ble.c executes
     * only its reviewed nonblocking get-info/address/log/permission prefix.
     * Scheduler locking postpones thread selection in that small window. The
     * native settings backend may block AFTER it has copied the captured slot;
     * a later selection cannot redirect that already captured settings write.
     * This is NOT an IRQ lock and does not cover ISR profile selection or an
     * unreviewed SDK that blocks/yields before capturing its profile index.
     * No policy spinlock is held across the gate or native callback.
     */
    k_sched_lock();
    bool allowed = pairing_gate(conn, true, &is_host);
    if (allowed && native_auth_info.pairing_complete != NULL) {
        native_auth_info.pairing_complete(conn, bonded);
    }
    k_sched_unlock();
    if (!allowed) { reject_pairing(conn); }
    else { clear_pairing(conn); bbp_ble_single_host_reconcile(); }
}

static void failed_pairing_cleanup(struct bt_conn *conn) {
    struct bt_conn_info info;
    int err = bt_conn_get_info(conn, &info);
    bool managed = err == 0 && host_connection(&info);
    if (err != 0) {
        /* A provisional owner was already positively classified as a managed
         * host. This RAM-only fallback can release it even if get_info fails;
         * it never guesses that an unknown central/nondefault link is a host.
         */
        uint8_t i = bt_conn_index(conn);
        if (i < CONFIG_BT_MAX_CONN) {
            k_spinlock_key_t key = k_spin_lock(&policy_lock);
            managed = open_owner.valid && open_owner.index == i &&
                      slots[i].token == conn && open_owner.lifetime == slots[i].lifetime;
            k_spin_unlock(&policy_lock, key);
        }
    }
    if (managed) { reject_pairing(conn); }
    else { clear_pairing(conn); }
}

static void guarded_pairing_failed(struct bt_conn *conn, enum bt_security_err reason) {
    /* SMP failure/cancel does not promise that the peer will drop its ACL.
     * Release provisional admission and arrange a host-only disconnect, never
     * unpair. An ordinary successful completion uses clear_pairing instead.
     */
    failed_pairing_cleanup(conn);
    if (native_auth_info.pairing_failed != NULL) { native_auth_info.pairing_failed(conn, reason); }
}

static void guarded_auth_cancel(struct bt_conn *conn) {
    failed_pairing_cleanup(conn);
    if (native_auth.cancel != NULL) { native_auth.cancel(conn); }
}

int bbp_ble_single_host_auth_cb_register(const struct bt_conn_auth_cb *cb) {
    if (cb == NULL) {
        int err = bt_conn_auth_cb_register(NULL);
        if (err == 0) { auth_registered = false; }
        return err;
    }
    if (auth_registered) { return -EALREADY; }
    native_auth = *cb;
    guarded_auth = *cb;
    guarded_auth.pairing_accept = guarded_pairing_accept;
    if (cb->cancel != NULL) { guarded_auth.cancel = guarded_auth_cancel; }
    int err = bt_conn_auth_cb_register(&guarded_auth);
    if (err == 0) { auth_registered = true; }
    return err;
}

int bbp_ble_single_host_auth_info_cb_register(struct bt_conn_auth_info_cb *cb) {
    if (cb == NULL) { return -EINVAL; }
    if (auth_info_registered) { return -EALREADY; }
    native_auth_info = *cb;
    guarded_auth_info = *cb;
    memset(&guarded_auth_info.node, 0, sizeof(guarded_auth_info.node));
    guarded_auth_info.pairing_complete = guarded_pairing_complete;
    guarded_auth_info.pairing_failed = guarded_pairing_failed;
    int err = bt_conn_auth_info_cb_register(&guarded_auth_info);
    if (err == 0) { auth_info_registered = true; }
    return err;
}

#else
void bbp_ble_single_host_reconcile(void) {}
#endif
