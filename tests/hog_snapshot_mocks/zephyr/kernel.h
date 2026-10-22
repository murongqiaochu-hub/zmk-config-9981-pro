/* SPDX-License-Identifier: MIT -- dedicated host test mock */
#ifndef HOG_SNAPSHOT_MOCK_KERNEL_H
#define HOG_SNAPSHOT_MOCK_KERNEL_H
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define IS_ENABLED(x) (x)
#define BUILD_ASSERT(expr, ...) _Static_assert(expr, "mock BUILD_ASSERT")
#define __ASSERT_NO_MSG(expr) assert(expr)
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
int k_msgq_peek(struct k_msgq *q, void *data);
unsigned int k_msgq_num_used_get(struct k_msgq *q);
struct k_work_q { bool started, plugged; };
struct k_work { void (*handler)(struct k_work *); };
struct k_work_delayable {
    struct k_work work;
    struct k_work_q *queue;
    int64_t due;
    bool queued, running, delayed, canceling;
};
#define K_WORK_DELAYABLE_DEFINE(name, fn) \
    struct k_work_delayable name = {.work = {.handler = (fn)}}
extern struct k_work_q k_sys_work_q;
int k_work_schedule_for_queue(struct k_work_q *queue, struct k_work_delayable *work,
                              k_timeout_t delay);
int64_t k_uptime_get(void);
#endif
