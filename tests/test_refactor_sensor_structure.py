#!/usr/bin/env python3
"""Rebuild the sensor split from v2.2.9 using only named mechanical edits.
Checks the whole remaining driver, not just a selection of its functions.
"""
import re
from refactor_support import ROOT, DRIVER, reference, tokens, function


def cut(text, start, end):
    a=text.index(start);b=text.index(end,a)
    return text[:a]+text[b:],text[a:b]


def main():
    source=reference(DRIVER+'a320_0x57.c').decode().replace('\r\n','\n')
    expected,states=cut(source,'static struct tp_key_state scroll_keys;','/* === Configure Motion GPIO === */')
    expected,listener=cut(expected,'/* Bit 0 is physical Ctrl;',
                          '/* =========================\n *   Work handler (polling)')
    expected,api=cut(expected,'bool tp_scroll_mode_active(void)',
                     '/* =========================\n *   Device init')
    expected=expected.replace('#include "aa_sym_ctrl.h"\n','')
    expected=expected.replace('#include "trackpad_key_state.h"',
                              '#include "trackpad_key_state.h"\n#include "trackpad_mode.h"')
    expected=expected.replace('#include "trackpad_pointer.h"',
                              '#include "trackpad_pointer.h"\n#include "trackpad_scroll.h"')
    expected=expected.replace('static int16_t sum_dx = 0, sum_dy = 0;\nstatic uint16_t sample_cnt = 0;',
                              'static struct tp_scroll_state scroll_motion;')
    expected=expected.replace('static uint32_t last_read_time = 0;\n','')
    expected=expected.replace('static int16_t shoulder_scroll_remainder_x, shoulder_scroll_remainder_y;\n','')
    expected,half=cut(expected,'/* Halve discrete wheel reports without discarding every odd notch. */',
                      '/* =========================\n *   Data & Config structs')
    a=expected.index('                /* --- Condition 1: dx=0 && dy=0')
    b=expected.index('\n            }\n        } else {',a)
    wheel=expected[a:b]
    expected=expected[:a]+'''                uint16_t threshold = MAX(1, 40 / CONFIG_A320_POLL_INTERVAL_MS);
                int16_t wheel_x, wheel_y;
                if (tp_scroll_push(&scroll_motion, dx, dy, now, threshold, scroll_locked,
                                   scroll_rotation, shoulder_slow, &wheel_x, &wheel_y)) {
                    a320_report_rel(dev, INPUT_REL_HWHEEL, wheel_x, false, K_FOREVER);
                    a320_report_rel(dev, INPUT_REL_WHEEL, wheel_y, true, K_FOREVER);
                }'''+expected[b:]
    mapping={'sum_dx':'scroll_motion.sum_dx','sum_dy':'scroll_motion.sum_dy',
             'sample_cnt':'scroll_motion.sample_cnt','last_read_time':'scroll_motion.last_read_time',
             'shoulder_scroll_remainder_x':'scroll_motion.remainder_x',
             'shoulder_scroll_remainder_y':'scroll_motion.remainder_y'}
    for before,after in mapping.items():expected=re.sub(r'\b'+before+r'\b',after,expected)
    # Unify only the six complete reset sequences, never a partial/zero-frame reset.
    for assignments in [
        r'scroll_motion.sum_dx = scroll_motion.sum_dy = 0;\s*scroll_motion.sample_cnt = 0;\s*scroll_motion.last_read_time = 0;\s*scroll_motion.remainder_x = scroll_motion.remainder_y = 0;',
        r'scroll_motion.sum_dx = 0;\s*scroll_motion.sum_dy = 0;\s*scroll_motion.sample_cnt = 0;\s*scroll_motion.last_read_time = 0;\s*scroll_motion.remainder_x = scroll_motion.remainder_y = 0;']:
        expected=re.sub(assignments,'tp_scroll_reset(&scroll_motion);',expected)
    for snapshot,getter in [('scroll_mode_snapshot','tp_mode_scroll_snapshot'),
                            ('cursor_speed_snapshot','tp_mode_speed_snapshot'),
                            ('scroll_shoulder_modifier_snapshot','tp_mode_shoulder_snapshot')]:
        expected=expected.replace('(uint32_t)atomic_get(&'+snapshot+')',getter+'()')
    expected=expected.replace('atomic_set(&scroll_shoulder_modifier_snapshot, 0);','tp_mode_init();')
    actual=(ROOT/DRIVER/'a320_0x57.c').read_text()
    assert tokens(expected)==tokens(actual),'driver changed beyond enumerated extraction/state/getter/reset edits'

    mode=(ROOT/DRIVER/'trackpad_mode.c').read_text()
    # All moved mode state, listener, registration and public API tokens stay in order.
    a=mode.index('static struct tp_key_state scroll_keys;')
    b=mode.index('uint32_t tp_mode_scroll_snapshot(void)')
    assert tokens(mode[a:b])==tokens(states+listener+api),'mode publication/order changed'
    scroll=(ROOT/DRIVER/'trackpad_scroll.h').read_text()
    old_half=function(half,'scroll_half_with_remainder').replace('scroll_half_with_remainder','tp_scroll_half').replace('static int16_t','static inline int16_t')
    assert tokens(old_half)==tokens(function(scroll,'tp_scroll_half'))
    assert tokens(function(scroll,'tp_scroll_reset'))==tokens('''static inline void tp_scroll_reset(struct tp_scroll_state *s) {
        s->sum_dx=s->sum_dy=0;s->sample_cnt=0;s->last_read_time=0;s->remainder_x=s->remainder_y=0;
    }''')
    # The old gain decision tree itself is frozen, independently of the replays.
    old_gain=wheel[wheel.index('if (abs(sy) >= 128)'):wheel.index('int16_t wheel_x = -scroll_x;')]
    new_gain=scroll[scroll.index('if (abs(sy) >= 128)'):scroll.index('*wheel_x = -scroll_x;')]
    assert tokens(old_gain)==tokens(new_gain),'wheel gains/rounding changed'
    print('PASS full sensor mechanical proof: original driver tokens restored after named extractions; mode state/order, half arithmetic, complete resets and wheel gain tree exact')


if __name__=='__main__':main()
