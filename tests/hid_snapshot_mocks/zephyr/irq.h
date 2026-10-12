/* Deterministic IRQ/preemption instrumentation is defined in test_sdk_hid.c. */
#pragma once
unsigned int hid_mock_irq_lock(void);
void hid_mock_irq_unlock(unsigned int key);
#define irq_lock() hid_mock_irq_lock()
#define irq_unlock(key) hid_mock_irq_unlock(key)
