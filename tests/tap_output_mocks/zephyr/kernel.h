/* Host mock of the Zephyr API used by the actual tap output translation unit. */
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define IS_ENABLED(x) (x)
#define K_MSEC(ms) ((uint32_t)(ms))
#define K_LOWEST_APPLICATION_THREAD_PRIO 14
#define K_THREAD_STACK_DEFINE(name, size) static unsigned char name[size]
#define K_THREAD_STACK_SIZEOF(name) sizeof(name)

struct k_spinlock { bool locked; };
typedef unsigned int k_spinlock_key_t;
static inline k_spinlock_key_t k_spin_lock(struct k_spinlock *lock) {
    assert(!lock->locked);
    lock->locked = true;
    return 1;
}
static inline void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key) {
    assert(key == 1 && lock->locked);
    lock->locked = false;
}
struct k_work { void (*handler)(struct k_work *); };
struct k_work_delayable {
    struct k_work work;
    bool pending;
    uint32_t delay;
    unsigned int schedules;
};
struct k_work_q { bool started; };
struct k_work_queue_config { const char *name; };

extern uint32_t mock_now;
extern int mock_schedule_error;
extern unsigned int mock_queue_starts;
static inline uint32_t k_uptime_get_32(void) { return mock_now; }
static inline void k_work_init_delayable(struct k_work_delayable *work,
                                         void (*handler)(struct k_work *)) {
    memset(work, 0, sizeof(*work));
    work->work.handler = handler;
}
static inline void k_work_queue_start(struct k_work_q *queue, void *stack, size_t size,
                                      int priority, const struct k_work_queue_config *config) {
    assert(stack && size >= 1024 && priority == K_LOWEST_APPLICATION_THREAD_PRIO);
    assert(config && config->name && !queue->started);
    queue->started = true;
    mock_queue_starts++;
}
static inline int k_work_reschedule_for_queue(struct k_work_q *queue,
                                              struct k_work_delayable *work, uint32_t delay) {
    assert(queue->started && work->work.handler);
    if (mock_schedule_error) {
        return mock_schedule_error;
    }
    work->pending = true;
    work->delay = delay;
    work->schedules++;
    return 1;
}
