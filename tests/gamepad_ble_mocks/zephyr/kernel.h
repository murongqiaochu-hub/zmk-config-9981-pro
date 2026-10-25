/* SPDX-License-Identifier: MIT -- deterministic host-only kernel model. */
#pragma once
#include <zephyr/sys/util.h>
typedef int64_t k_timeout_t;
#define K_NO_WAIT INT64_C(0)
#define K_MSEC(ms) ((k_timeout_t)(ms))
struct k_spinlock { bool held; };
typedef unsigned int k_spinlock_key_t;
k_spinlock_key_t k_spin_lock(struct k_spinlock *lock);
void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key);
struct k_msgq {
    unsigned char *buffer;
    size_t item_size;
    unsigned int capacity, used, read_pos, write_pos;
};
#define K_MSGQ_DEFINE(name, size, depth, align) \
    static unsigned char name##_buffer[(size) * (depth)] __attribute__((aligned(align))); \
    struct k_msgq name = {.buffer = name##_buffer, .item_size = (size), .capacity = (depth)}
int k_msgq_put(struct k_msgq *q, const void *data, k_timeout_t timeout);
int k_msgq_get(struct k_msgq *q, void *data, k_timeout_t timeout);
unsigned int k_msgq_num_used_get(struct k_msgq *q);
struct k_work_q { bool started, plugged; };
struct k_work { void (*handler)(struct k_work *); };
struct k_work_delayable {
    struct k_work work;
    struct k_work_q *queue;
    int64_t due;
    bool queued;
};
#define K_WORK_DEFINE(name, fn) struct k_work name = {.handler = (fn)}
#define K_WORK_DELAYABLE_DEFINE(name, fn) \
    struct k_work_delayable name = {.work = {.handler = (fn)}}
#define K_THREAD_STACK_DEFINE(name, size) static unsigned char name[size]
#define K_THREAD_STACK_SIZEOF(name) sizeof(name)
struct k_work_queue_config { const char *name; };
extern struct k_work_q k_sys_work_q;
int k_work_schedule_for_queue(struct k_work_q *queue, struct k_work_delayable *work,
                               k_timeout_t delay);
int k_work_submit_to_queue(struct k_work_q *queue, struct k_work *work);
void k_work_queue_start(struct k_work_q *queue, void *stack, size_t size, int priority,
                        const struct k_work_queue_config *config);
int64_t k_uptime_get(void);
