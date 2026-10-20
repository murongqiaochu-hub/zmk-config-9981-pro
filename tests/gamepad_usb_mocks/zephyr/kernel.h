/* Deterministic single-core semaphore/work surface. */
#pragma once
#include <errno.h>
#include <zephyr/sys/util.h>
struct k_sem { unsigned int count, limit; };
#define K_SEM_DEFINE(name, initial, maximum) struct k_sem name = {initial, maximum}
#define K_MSEC(ms) (ms)
int k_sem_take(struct k_sem *sem, int timeout);
void k_sem_give(struct k_sem *sem);
bool k_is_in_isr(void);
struct k_work { void (*handler)(struct k_work *); };
#define K_WORK_DEFINE(name, cb) struct k_work name = {cb}
int k_work_submit(struct k_work *work);
