/* Link the actual production pipeline.c, override ONLY the hardware provider and output API. */
#include <assert.h>
#include <stdio.h>
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_tap_pipeline.h"
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_tap_output.h"
static struct tp_tap_verified_contact sensor;
static struct tp_tap_output_context last_context;
static int references,submit_error;
static unsigned submits,cancels,inits;
struct tp_tap_verified_contact tp_tap_contact_provider_read(uint32_t now){(void)now;return sensor;}
int bbp9981_hid_mouse_button_references(unsigned int button){assert(button==0);return references;}
int tp_tap_output_init(void){inits++;return 0;}
int tp_tap_output_submit(void){assert(last_context.enabled&&last_context.physical_left_known&&!last_context.physical_left_down);submits++;return submit_error;}
int tp_tap_output_cancel(void){cancels++;return 0;}
int tp_tap_output_update_context(struct tp_tap_output_context c){last_context=c;return 0;}
struct tp_tap_output_inflight tp_tap_output_read_inflight(void){return (struct tp_tap_output_inflight){0};}
static void frame(uint32_t t,enum tp_tap_contact c,int dx,bool fault,bool scroll,uint32_t mode){sensor.contact=c;sensor.observed_at=t;tp_tap_pipeline_poll(t,dx,0,fault,scroll,mode);}
static void arm(uint32_t t){sensor.source_epoch++;frame(t,TP_TAP_UP,0,false,false,0);frame(t+10,TP_TAP_UP,0,false,false,0);frame(t+30,TP_TAP_UP,0,false,false,0);assert(tp_tap_pipeline_status().state==TP_TAP_ARMED);}
static void gesture(uint32_t t){frame(t,TP_TAP_DOWN,1,false,false,0);frame(t+20,TP_TAP_DOWN,-1,false,false,0);frame(t+60,TP_TAP_UP,0,false,false,0);frame(t+80,TP_TAP_UP,0,false,false,0);}
int main(void){
 tp_tap_pipeline_init();tp_tap_pipeline_init();assert(inits==1);
 sensor.valid=true; /* valid alone does not prove hardware protocol! */
 frame(0,TP_TAP_UP,0,false,false,0);frame(20,TP_TAP_UP,0,false,false,0);gesture(40);assert(submits==0&&!last_context.enabled);
 sensor.protocol_verified=true;sensor.valid=true;arm(200);gesture(240);assert(submits==1&&tp_tap_pipeline_status().click_ready);frame(330,TP_TAP_UP,0,false,false,0);assert(submits==1);
 arm(400);frame(440,TP_TAP_DOWN,13,false,false,0);frame(460,TP_TAP_DOWN,-13,false,false,0);frame(500,TP_TAP_UP,0,false,false,0);frame(520,TP_TAP_UP,0,false,false,0);assert(submits==1);
 arm(600);frame(640,TP_TAP_DOWN,0,false,false,0);frame(660,TP_TAP_DOWN,0,true,false,0);frame(700,TP_TAP_UP,0,false,false,0);frame(720,TP_TAP_UP,0,false,false,0);assert(submits==1);
 arm(800);frame(840,TP_TAP_DOWN,0,false,false,0);frame(860,TP_TAP_DOWN,0,false,false,2);frame(900,TP_TAP_UP,0,false,false,2);frame(920,TP_TAP_UP,0,false,false,2);assert(submits==1&&cancels>0);
 arm(1000);references=1;gesture(1040);assert(submits==1&&last_context.physical_left_down);references=0;
 arm(1200);frame(1240,TP_TAP_DOWN,0,false,true,0);frame(1260,TP_TAP_DOWN,0,false,true,0);frame(1300,TP_TAP_UP,0,false,false,0);frame(1320,TP_TAP_UP,0,false,false,0);assert(submits==1);
 arm(1400);sensor.observed_at=1400;sensor.contact=TP_TAP_DOWN;tp_tap_pipeline_poll(1440,0,0,false,false,0);assert(!last_context.enabled&&!tp_tap_pipeline_status().click_ready);assert(submits==1);
 arm(1600);submit_error=-1;gesture(1640);assert(submits==2&&tp_tap_pipeline_status().state==TP_TAP_WAIT_UP);
 references=-1;frame(1740,TP_TAP_UP,0,false,false,0);assert(!last_context.enabled&&!last_context.physical_left_known);
 puts("PASS Actual pipeline source: verified/fresh contact required, raw peak cancels out-and-back, real submit boundary, motion fault/mode/scroll/foreign-left/SDK failure invalidation, unknown provider emits zero clicks");
 return 0;
}
