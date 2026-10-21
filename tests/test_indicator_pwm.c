/* Standalone shared-backend differential/transaction test; no hardware claims. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/* The normal host stub supplies no declarations. Keep this mock independent of
 * test_env.h so zero, one and UINT32_MAX periods are not constrained to 500 us. */
struct pwm_dt_spec { uint32_t channel, period; };

enum event_kind { READY, WRITE, ERROR };
struct pwm_event { enum event_kind kind; int channel, error; };
#define CHANNELS 4
static const struct pwm_dt_spec pwms[CHANNELS] = {
    {.channel = 0, .period = 0U},
    {.channel = 1, .period = 1U},
    {.channel = 2, .period = 500000U},
    {.channel = 3, .period = UINT32_MAX},
};
static struct {
    uint32_t pulses[CHANNELS], cached[CHANNELS], before[CHANNELS];
    uint32_t expected[CHANNELS], output[CHANNELS];
    bool valid, valid_before, ready[CHANNELS];
    int write_result[CHANNELS], count;
    struct pwm_event events[CHANNELS * 3];
    unsigned event_count;
} fixture;

static void observe_before_commit(void) {
    assert(fixture.valid == fixture.valid_before);
    for (int i = 0; i < fixture.count; i++) {
        /* This must already hold even at the FIRST ready call. */
        assert(fixture.pulses[i] == fixture.expected[i]);
        assert(fixture.cached[i] == fixture.before[i]);
    }
}
static void record_event(enum event_kind kind, int channel, int error) {
    observe_before_commit();
    assert(fixture.event_count < CHANNELS * 3U);
    fixture.events[fixture.event_count++] = (struct pwm_event){kind, channel, error};
}
static bool pwm_is_ready_dt(const struct pwm_dt_spec *pwm) {
    assert(pwm == &pwms[pwm->channel]);
    record_event(READY, (int)pwm->channel, 0);
    return fixture.ready[pwm->channel];
}
static int pwm_set_dt(const struct pwm_dt_spec *pwm, uint32_t period, uint32_t pulse) {
    assert(period == pwm->period && pulse == fixture.expected[pwm->channel]);
    record_event(WRITE, (int)pwm->channel, 0);
    int result = fixture.write_result[pwm->channel];
    if (result >= 0) { fixture.output[pwm->channel] = pulse; }
    return result;
}
static void report_error(int channel, int error) { record_event(ERROR, channel, error); }

#include "../config/boards/arm/bbp9981/custom_driver/indicator_brightness.h"
#include "../config/boards/arm/bbp9981/custom_driver/indicator_pwm.h"

/* Frozen pre-extraction arithmetic: deliberately do not use the new helper. */
static uint32_t old_user_limit(uint8_t raw) {
    unsigned value = raw > 100U ? 100U : raw;
    if (value == 0U) { return 0U; }
    unsigned limit = (value * value + 50U) / 100U;
    return limit == 0U ? 1U : limit;
}
static uint32_t old_keyboard_pulse(uint32_t period, uint8_t pattern, uint8_t raw) {
    uint32_t fraction = (uint32_t)(pattern > 100U ? 100U : pattern) * old_user_limit(raw);
    if (fraction > 10000U) { fraction = 10000U; }
    return (uint32_t)((uint64_t)period * fraction / 10000U);
}
static uint32_t old_trackpad_pulse(uint32_t period, uint8_t pattern, uint8_t raw) {
    uint32_t bounded_pattern = pattern > 100U ? 100U : pattern;
    uint32_t limit = old_user_limit(raw);
    uint64_t scaled = (uint64_t)period * bounded_pattern * limit;
    return (uint32_t)(scaled / (10000U * 3U));
}
static void test_all_period_raw_pattern_pairs(void) {
    unsigned pairs = 0;
    for (int channel = 0; channel < CHANNELS; channel++) {
        uint32_t period = pwms[channel].period;
        for (unsigned raw = 0; raw <= UINT8_MAX; raw++) {
            assert(indicator_user_limit((uint8_t)raw) == old_user_limit((uint8_t)raw));
            for (unsigned pattern = 0; pattern <= UINT8_MAX; pattern++) {
                uint32_t bounded = pattern > 100U ? 100U : pattern;
                uint32_t fraction = bounded * indicator_user_limit((uint8_t)raw);
                uint32_t kb = old_keyboard_pulse(period, (uint8_t)pattern, (uint8_t)raw);
                uint32_t tp = old_trackpad_pulse(period, (uint8_t)pattern, (uint8_t)raw);
                assert(indicator_pwm_pulse_ns(period, fraction, 10000U) == kb);
                assert(indicator_pwm_pulse_ns(period, fraction, 30000U) == tp);
                assert(kb <= period && tp <= period / 3U);
                assert(tp == kb / 3U); /* Nested floor equals one final /30000. */
                assert(indicator_pattern_level((uint8_t)pattern, (uint8_t)raw) ==
                       (fraction + 50U) / 100U);
                if (raw == 0U || pattern == 0U) { assert(kb == 0U && tp == 0U); }
                pairs++;
            }
        }
    }
    assert(pairs == 4U * 256U * 256U);
    assert(indicator_pwm_pulse_ns(UINT32_MAX, 10000U, 10000U) == UINT32_MAX);
    assert(indicator_pwm_pulse_ns(UINT32_MAX, 10000U, 30000U) == UINT32_MAX / 3U);
    printf("PASS indicator PWM differential: %u period/raw/pattern vectors per old KB/TP path, 64-bit product and exact downward rounding\n", pairs);
}

static void begin_operation(uint32_t fraction, uint32_t denominator) {
    fixture.event_count = 0;
    fixture.valid_before = fixture.valid;
    memcpy(fixture.before, fixture.cached, sizeof(fixture.before));
    for (int i = 0; i < CHANNELS; i++) {
        fixture.pulses[i] = UINT32_MAX;
        fixture.expected[i] = (uint32_t)((uint64_t)pwms[i].period * fraction / denominator);
    }
}
static enum indicator_pwm_result apply(uint32_t fraction, uint32_t denominator) {
    begin_operation(fraction, denominator);
    return indicator_pwm_apply(pwms, fixture.count, fraction, denominator,
                               fixture.pulses, fixture.cached, &fixture.valid, report_error);
}
static void reset_fixture(void) {
    memset(&fixture, 0, sizeof(fixture));
    fixture.count = CHANNELS;
    for (int i = 0; i < CHANNELS; i++) { fixture.ready[i] = true; }
}
static void expect_event(unsigned *index, enum event_kind kind, int channel, int error) {
    assert(*index < fixture.event_count);
    const struct pwm_event *event = &fixture.events[(*index)++];
    assert(event->kind == kind && event->channel == channel && event->error == error);
}
static void assert_transaction_trace(void) {
    unsigned index = 0;
    for (int i = 0; i < fixture.count; i++) {
        expect_event(&index, READY, i, 0);
        if (fixture.ready[i]) {
            expect_event(&index, WRITE, i, 0);
            if (fixture.write_result[i] < 0) {
                expect_event(&index, ERROR, i, fixture.write_result[i]);
            }
        } else {
            expect_event(&index, ERROR, i, -ENODEV);
        }
    }
    assert(index == fixture.event_count);
}
static void assert_applied(void) {
    assert_transaction_trace();
    assert(fixture.valid);
    for (int i = 0; i < fixture.count; i++) {
        assert(fixture.cached[i] == fixture.expected[i]);
        assert(fixture.output[i] == fixture.expected[i]);
    }
}
static void test_failure_retry(uint32_t denominator, int bad_channel, bool ready_failure) {
    reset_fixture();
    assert(apply(2500U, denominator) == INDICATOR_PWM_APPLIED);
    assert_applied();
    if (ready_failure) { fixture.ready[bad_channel] = false; }
    else { fixture.write_result[bad_channel] = -EIO; }
    assert(apply(5000U, denominator) == INDICATOR_PWM_FAILED);
    assert_transaction_trace();
    assert(!fixture.valid);
    for (int i = 0; i < CHANNELS; i++) {
        assert(fixture.cached[i] == fixture.before[i]);
        assert(fixture.output[i] == (i == bad_channel ? fixture.before[i] : fixture.expected[i]));
    }

    /* Retry the OLD cached fraction, not just the failed target. Invalid means
     * every channel must be rewritten even when computed == cached everywhere. */
    fixture.ready[bad_channel] = true;
    fixture.write_result[bad_channel] = 0;
    assert(apply(2500U, denominator) == INDICATOR_PWM_APPLIED);
    assert_applied();
    assert(apply(2500U, denominator) == INDICATOR_PWM_UNCHANGED);
    assert(fixture.valid && fixture.event_count == 0U);
    for (int i = 0; i < CHANNELS; i++) {
        assert(fixture.pulses[i] == fixture.expected[i]);
        assert(fixture.cached[i] == fixture.before[i]);
    }
}
static void test_transaction_edges(uint32_t denominator) {
    reset_fixture();
    /* An invalid, all-zero cache still performs every ready/write pair. */
    assert(apply(0U, denominator) == INDICATOR_PWM_APPLIED);
    assert_applied();
    /* A valid hit performs NO readiness checks, even if devices became unready. */
    for (int i = 0; i < CHANNELS; i++) { fixture.ready[i] = false; }
    assert(apply(0U, denominator) == INDICATOR_PWM_UNCHANGED);
    assert(fixture.valid && fixture.event_count == 0U);
    assert(apply(7500U, denominator) == INDICATOR_PWM_FAILED);
    assert_transaction_trace();
    assert(!fixture.valid);
    for (int i = 0; i < CHANNELS; i++) { assert(fixture.cached[i] == 0U); }

    /* Multiple failures continue through the last channel, and nonnegative
     * driver returns count as success exactly as in the old two loops. */
    fixture.ready[0] = true; fixture.write_result[0] = -EIO;
    fixture.ready[2] = true; fixture.write_result[2] = 7;
    assert(apply(7500U, denominator) == INDICATOR_PWM_FAILED);
    assert_transaction_trace();
    assert(!fixture.valid);
    for (int i = 0; i < CHANNELS; i++) { assert(fixture.cached[i] == 0U); }
    for (int i = 0; i < CHANNELS; i++) {
        fixture.ready[i] = true;
        fixture.write_result[i] = 7;
    }
    assert(apply(7500U, denominator) == INDICATOR_PWM_APPLIED);
    assert_applied();

    /* Period 0/1 outputs can stay identical when the fraction changes. Cache
     * comparison is pulse-based, not numerator-based, and prepares both slots. */
    reset_fixture(); fixture.count = 2;
    assert(apply(1U, denominator) == INDICATOR_PWM_APPLIED);
    assert_applied();
    assert(apply(2U, denominator) == INDICATOR_PWM_UNCHANGED);
    assert(fixture.valid && fixture.event_count == 0U);
    assert(fixture.pulses[0] == 0U && fixture.pulses[1] == 0U);
}
int main(void) {
    test_all_period_raw_pattern_pairs();
    const uint32_t denominators[] = {10000U, 30000U};
    for (unsigned d = 0; d < sizeof(denominators) / sizeof(denominators[0]); d++) {
        for (int bad = 0; bad < CHANNELS; bad++) {
            test_failure_retry(denominators[d], bad, false);
            test_failure_retry(denominators[d], bad, true);
        }
        test_transaction_edges(denominators[d]);
    }
    puts("PASS indicator PWM transaction: prepare-all first, ordered ready/write/error, continue after any failure, atomic cache commit, full retry, unchanged-hit silence and zero/one-period quantization");
    return 0;
}
