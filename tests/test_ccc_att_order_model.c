/* SPDX-License-Identifier: MIT
 * Abstract host model for a proposed per-peer CCC/app-queue admission gate.
 * This intentionally does NOT compile or call Zephyr GATT/ATT/connection code.
 *
 * The modeled linearization point is a fixed app-owned queue-slot admission,
 * not Zephyr's ATT queue and not an over-the-air send. Public notify is called
 * later, outside the model lock, and may block/fail exactly as the pinned SDK
 * allows. This checks only the app-level ordering contract a future integration
 * would need to implement.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef NDEBUG
#error "the ordering model requires enabled assertions"
#endif

enum { PEER_COUNT = 2U, QUEUE_CAPACITY = 4U, CCC_NOTIFY = 1U };

enum admission_result {
    ADMISSION_OK,
    ADMISSION_CLOSED,
    ADMISSION_STALE,
    ADMISSION_FULL,
};

struct report {
    uint8_t tag;
    bool neutral;
    uint32_t token;
};

struct gate_model {
    unsigned int lock_depth; /* Models a short app-owned shared critical section. */
    bool slot[PEER_COUNT];
    bool connected[PEER_COUNT];
    bool encrypted[PEER_COUNT];
    bool open[PEER_COUNT];
    uint16_t ccc[PEER_COUNT];
    uint32_t token[PEER_COUNT];
    struct report app_queue[PEER_COUNT][QUEUE_CAPACITY];
    unsigned int head[PEER_COUNT];
    unsigned int tail[PEER_COUNT];
    unsigned int queued[PEER_COUNT];
    unsigned int app_admitted[PEER_COUNT];
    unsigned int cancelled[PEER_COUNT];
    unsigned int claimed_for_sdk[PEER_COUNT];
    unsigned int public_notify_calls[PEER_COUNT];
};

static void enter(struct gate_model *m) {
    assert(m && m->lock_depth == 0U);
    m->lock_depth = 1U;
}

static void leave(struct gate_model *m) {
    assert(m && m->lock_depth == 1U);
    m->lock_depth = 0U;
}

static void init(struct gate_model *m) {
    memset(m, 0, sizeof(*m));
    for (unsigned int i = 0U; i < PEER_COUNT; ++i) {
        m->token[i] = 1U;
        m->connected[i] = true;
        m->encrypted[i] = true;
    }
}

static void cancel_pending(struct gate_model *m, unsigned int peer) {
    assert(m && peer < PEER_COUNT && m->lock_depth == 1U);
    m->cancelled[peer] += m->queued[peer];
    m->queued[peer] = 0U;
    m->head[peer] = m->tail[peer];
}

/*
 * Model the pre-commit callback. Zephyr returns early for a peer with no CCC
 * slot writing zero, so that case deliberately does not call this transition.
 * A nonzero first write has already secured a cfg slot before its callback.
 */
static bool ccc_precommit(struct gate_model *m, unsigned int peer, uint16_t value) {
    assert(m && peer < PEER_COUNT);
    if (!m->slot[peer] && value == 0U) {
        return false;
    }

    enter(m);
    if (value != 0U) {
        m->slot[peer] = true;
    }
    m->open[peer] = false;
    ++m->token[peer];
    cancel_pending(m, peer);
    leave(m);
    return true;
}

/* The host's per-peer cfg->value assignment, after cfg_write returned. */
static void ccc_commit(struct gate_model *m, unsigned int peer, uint16_t value) {
    assert(m && peer < PEER_COUNT);
    enter(m);
    m->ccc[peer] = value;
    m->slot[peer] = value != 0U;
    leave(m);
}

/* Proposed per-connection post-commit / resync callback, not stock Zephyr. */
static void gate_sync(struct gate_model *m, unsigned int peer) {
    assert(m && peer < PEER_COUNT);
    enter(m);
    m->open[peer] = m->slot[peer] &&
                    ((m->ccc[peer] & CCC_NOTIFY) != 0U) &&
                    m->connected[peer] && m->encrypted[peer];
    leave(m);
}

/* App-owned fixed-slot admission: this is NOT ATT queue admission. */
static enum admission_result app_admit(struct gate_model *m, unsigned int peer,
                                       uint32_t expected_token, struct report report) {
    assert(m && peer < PEER_COUNT);
    enter(m);
    enum admission_result result;
    if (expected_token != m->token[peer]) {
        result = ADMISSION_STALE;
    } else if (!m->open[peer] || !m->connected[peer] || !m->encrypted[peer] ||
               !m->slot[peer] || !(m->ccc[peer] & CCC_NOTIFY)) {
        result = ADMISSION_CLOSED;
    } else if (m->queued[peer] == QUEUE_CAPACITY) {
        result = ADMISSION_FULL;
    } else {
        report.token = expected_token;
        m->app_queue[peer][m->tail[peer]] = report;
        m->tail[peer] = (m->tail[peer] + 1U) % QUEUE_CAPACITY;
        ++m->queued[peer];
        ++m->app_admitted[peer];
        result = ADMISSION_OK;
    }
    leave(m);
    return result;
}

/* Claim one app slot for later public notify; it is already in-flight after this. */
static bool claim_for_sdk(struct gate_model *m, unsigned int peer, struct report *out) {
    assert(m && peer < PEER_COUNT && out);
    enter(m);
    while (m->queued[peer]) {
        struct report report = m->app_queue[peer][m->head[peer]];
        m->head[peer] = (m->head[peer] + 1U) % QUEUE_CAPACITY;
        --m->queued[peer];
        if (m->open[peer] && m->connected[peer] && m->encrypted[peer] &&
            m->slot[peer] && (m->ccc[peer] & CCC_NOTIFY) &&
            report.token == m->token[peer]) {
            *out = report;
            ++m->claimed_for_sdk[peer];
            leave(m);
            return true;
        }
        ++m->cancelled[peer];
    }
    leave(m);
    return false;
}

/* Simulates only entering the public notify API, outside the gate lock. */
static void public_notify_call(struct gate_model *m, unsigned int peer,
                               const struct report *report) {
    assert(m && peer < PEER_COUNT && report && m->lock_depth == 0U);
    ++m->public_notify_calls[peer];
}

/* Proposed early-disconnect hook; bonded CCC may remain stored. */
static void disconnect_begin(struct gate_model *m, unsigned int peer) {
    assert(m && peer < PEER_COUNT);
    enter(m);
    m->open[peer] = false;
    ++m->token[peer];
    m->connected[peer] = false;
    cancel_pending(m, peer);
    leave(m);
}

static void reconnect_closed(struct gate_model *m, unsigned int peer) {
    assert(m && peer < PEER_COUNT);
    enter(m);
    m->connected[peer] = true;
    m->encrypted[peer] = false;
    m->open[peer] = false;
    ++m->token[peer];
    leave(m);
}

static unsigned int aggregate(const struct gate_model *m) {
    unsigned int value = 0U;
    for (unsigned int i = 0U; i < PEER_COUNT; ++i) {
        if (m->ccc[i] > value) {
            value = m->ccc[i];
        }
    }
    return value;
}

static void subscribe(struct gate_model *m, unsigned int peer) {
    assert(ccc_precommit(m, peer, CCC_NOTIFY));
    ccc_commit(m, peer, CCC_NOTIFY);
    gate_sync(m, peer);
    assert(m->open[peer]);
}

/* Reproduce the old pre-commit epoch-only failure window. */
static void test_epoch_invalidation_is_not_a_barrier(void) {
    uint16_t committed_ccc = CCC_NOTIFY;
    uint32_t epoch = 7U;
    unsigned int admitted = 0U;

    /* cfg_write invalidates; worker takes a fresh token before host commit. */
    ++epoch;
    uint32_t fresh = epoch;
    if (fresh == epoch && (committed_ccc & CCC_NOTIFY)) {
        ++admitted;
    }
    assert(admitted == 1U);

    committed_ccc = 0U; /* Host commits OFF only after the unsafe admission. */
    assert(committed_ccc == 0U);
    puts("PASS counterexample: pre-commit epoch invalidation still admits using old CCC");
}

static void test_app_queue_disable_order_and_peer_isolation(void) {
    struct gate_model m;
    init(&m);
    subscribe(&m, 0U);
    subscribe(&m, 1U);
    assert(aggregate(&m) == CCC_NOTIFY);

    assert(app_admit(&m, 0U, m.token[0], (struct report){.tag=1U, .neutral=false}) == ADMISSION_OK);
    assert(m.queued[0] == 1U);

    /* OFF wins before worker claim: close and cancel pending app-owned work. */
    assert(ccc_precommit(&m, 0U, 0U));
    assert(app_admit(&m, 0U, m.token[0], (struct report){.tag=2U, .neutral=false}) == ADMISSION_CLOSED);
    assert(app_admit(&m, 1U, m.token[1], (struct report){.tag=3U, .neutral=false}) == ADMISSION_OK);
    ccc_commit(&m, 0U, 0U);
    gate_sync(&m, 0U);

    struct report report;
    assert(!claim_for_sdk(&m, 0U, &report));
    assert(m.cancelled[0] == 1U && m.public_notify_calls[0] == 0U);
    assert(!m.open[0] && m.open[1]);
    assert(aggregate(&m) == CCC_NOTIFY); /* Peer 1 remains subscribed. */
    assert(m.queued[1] == 1U);
    puts("PASS app-queue ordering: OFF cancels unclaimed local work and leaves peer 1 independent");

    /* If the worker claimed first, that item is already in-flight and cannot be recalled. */
    init(&m);
    subscribe(&m, 0U);
    assert(app_admit(&m, 0U, m.token[0], (struct report){.tag=4U, .neutral=true}) == ADMISSION_OK);
    assert(claim_for_sdk(&m, 0U, &report));
    assert(ccc_precommit(&m, 0U, 0U));
    ccc_commit(&m, 0U, 0U);
    gate_sync(&m, 0U);
    public_notify_call(&m, 0U, &report); /* May occur after OFF, but claim linearized before OFF. */
    assert(m.public_notify_calls[0] == 1U && m.queued[0] == 0U);
    puts("PASS in-flight boundary: a pre-OFF claim may enter public notify later; no remote delivery claimed");
}

static void test_opposite_interleaving_and_enable_order(void) {
    struct gate_model m;
    init(&m);
    subscribe(&m, 0U);
    assert(ccc_precommit(&m, 0U, 0U));
    assert(app_admit(&m, 0U, m.token[0], (struct report){.tag=5U, .neutral=false}) == ADMISSION_CLOSED);
    ccc_commit(&m, 0U, 0U);
    gate_sync(&m, 0U);
    assert(m.queued[0] == 0U && !m.open[0]);

    /* Enable remains closed across pre-commit and commit; open only on post-commit sync. */
    assert(ccc_precommit(&m, 0U, CCC_NOTIFY));
    assert(app_admit(&m, 0U, m.token[0], (struct report){.tag=6U, .neutral=false}) == ADMISSION_CLOSED);
    ccc_commit(&m, 0U, CCC_NOTIFY);
    assert(app_admit(&m, 0U, m.token[0], (struct report){.tag=7U, .neutral=false}) == ADMISSION_CLOSED);
    gate_sync(&m, 0U);
    assert(m.open[0]);
    assert(app_admit(&m, 0U, m.token[0], (struct report){.tag=8U, .neutral=false}) == ADMISSION_OK);
    puts("PASS both CCC edges: OFF closes before commit; ON opens only after commit");
}

static void test_slotless_zero_is_a_noop(void) {
    struct gate_model m;
    init(&m);
    uint32_t generation = m.token[0];
    assert(!ccc_precommit(&m, 0U, 0U));
    assert(m.token[0] == generation && !m.open[0] && !m.slot[0]);
    assert(app_admit(&m, 0U, generation, (struct report){.tag=9U, .neutral=false}) == ADMISSION_CLOSED);
    puts("PASS slotless zero: a correctly initialized closed peer needs no managed callback");
}

static void test_disconnect_restore_security_and_stale_token(void) {
    struct gate_model m;
    init(&m);
    subscribe(&m, 0U);
    uint32_t old_token = m.token[0];

    disconnect_begin(&m, 0U);
    assert(!m.open[0] && m.ccc[0] == CCC_NOTIFY); /* Bonded value can persist. */
    assert(app_admit(&m, 0U, m.token[0], (struct report){.tag=10U, .neutral=true}) == ADMISSION_CLOSED);

    reconnect_closed(&m, 0U);
    /* Settings restore directly; this path does not call cfg_write. */
    enter(&m);
    m.slot[0] = true;
    m.ccc[0] = CCC_NOTIFY;
    leave(&m);
    gate_sync(&m, 0U); /* Still unencrypted, so remain closed. */
    assert(!m.open[0]);
    assert(app_admit(&m, 0U, old_token, (struct report){.tag=11U, .neutral=false}) == ADMISSION_STALE);

    enter(&m);
    m.encrypted[0] = true;
    leave(&m);
    gate_sync(&m, 0U); /* A successful security/update path may reopen this peer. */
    assert(m.open[0]);
    assert(app_admit(&m, 0U, old_token, (struct report){.tag=12U, .neutral=false}) == ADMISSION_STALE);
    assert(app_admit(&m, 0U, m.token[0], (struct report){.tag=13U, .neutral=false}) == ADMISSION_OK);
    puts("PASS disconnect/restore/security: close early, restore closed, reopen after security; old tokens stale");
}

int main(void) {
    test_epoch_invalidation_is_not_a_barrier();
    test_app_queue_disable_order_and_peer_isolation();
    test_opposite_interleaving_and_enable_order();
    test_slotless_zero_is_a_noop();
    test_disconnect_restore_security_and_stale_token();
    puts("LIMIT app-owned queue/gate model only; public notify may block/fail; no Zephyr ATT admission, ACK, ARM, radio, host or device proof");
    return 0;
}
