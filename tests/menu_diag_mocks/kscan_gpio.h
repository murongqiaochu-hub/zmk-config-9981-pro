#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct kscan_gpio { size_t index; };
struct kscan_gpio_port_state { int calls; };
int kscan_gpio_pin_get(const struct kscan_gpio *,struct kscan_gpio_port_state *);
