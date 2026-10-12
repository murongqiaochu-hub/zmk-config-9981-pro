/* Evaluate the unchanged SDK log arguments; assert IRQ protection at mutations. */
#pragma once
void hid_mock_log(const char *function, int error, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
#define LOG_MODULE_DECLARE(...)
#define LOG_DBG(...) hid_mock_log(__func__, 0, __VA_ARGS__)
#define LOG_ERR(...) hid_mock_log(__func__, 1, __VA_ARGS__)
