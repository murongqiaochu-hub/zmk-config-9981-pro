#include "test_env.h"
#include "../config/boards/arm/bbp9981/custom_driver/keyboard_backlight.c"
int main(void) {
    assert(INDICATOR_LED_NUM_LEDS==2&&keyboardbacklight_init()==0);
    fake_brightness=1;fake_rgb_on=true;fake_activity=ZMK_ACTIVITY_ACTIVE;fake_layer=2;
    zmk_event_t key={.valid=true,.data={.position=41,.state=true}};key_listener_cb(&key);
    polling_work_handler(&polling_work.work);
    bool seen[81]={0};unsigned count=0;
    for(unsigned i=0;i<200U;i++) {
        cycle_work_handler(&cycle_work.work);
        uint32_t ns=(uint32_t)((uint64_t)500000U*requested_pattern/10000U);
        assert(fake_pwm_pulse_ns==ns&&fake_pwm_ticks>=8U&&fake_pwm_ticks<=80U);
        assert(fake_pwm_period_ns==500000U);
        if(!seen[fake_pwm_ticks]){seen[fake_pwm_ticks]=true;count++;}
        unsigned writes=fake_pwm_writes;polling_work_handler(&polling_work.work);
        assert(fake_pwm_writes==writes);
        assert(fake_pwm_pulses[0]==ns&&fake_pwm_pulses[1]==ns);
    }
    assert(count>=18U&&seen[8]&&seen[80]);
    for(unsigned setting=0;setting<=255U;setting++)for(unsigned p=0;p<=100U;p++) {
        fake_brightness=(uint8_t)setting;fake_rgb_on=true;set_mode_brightness((uint8_t)p);
        uint32_t expected=(uint32_t)((uint64_t)500000U*p*indicator_user_limit((uint8_t)setting)/10000U);
        assert(fake_pwm_pulse_ns==expected&&fake_pwm_ticks<=80U*indicator_user_limit((uint8_t)setting));
        if(setting==0U||p==0U)assert(fake_pwm_pulse_ns==0U);
    }
    fake_brightness=1;fake_rgb_on=false;set_mode_brightness(100);assert(fake_pwm_pulse_ns==0U);
    fake_rgb_on=true;fake_led_error=-EIO;fake_led_error_index=1;set_mode_brightness(25);
    assert(!kb_pwm_valid&&last_led_level==-1);fake_led_error=0;set_mode_brightness(25);
    assert(kb_pwm_valid&&fake_pwm_pulses[0]==1250U&&fake_pwm_pulses[1]==1250U);
    printf("PASS keyboard all-path fractional PWM: %u low-cycle levels on BOTH channels, 2kHz, all256 caps, zero/B-off, poll cache and partial retry; no hardware visual claim\n",count);
    return 0;
}
