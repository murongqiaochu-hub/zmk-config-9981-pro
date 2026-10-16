#pragma once
#include <stdint.h>
#include <stdbool.h>
struct zmk_debounce_state { bool pressed,changed;uint16_t counter; };
struct zmk_debounce_config { uint32_t debounce_press_ms,debounce_release_ms; };
void zmk_debounce_update(struct zmk_debounce_state *,bool,int,const struct zmk_debounce_config *);
