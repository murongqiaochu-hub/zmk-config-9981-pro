#define CONFIG_BBP9981_MENU_DIAG 1
#include <stdint.h>
struct zmk_hid_keyboard_report_body {uint8_t modifiers,reserved,keys[6];};
static unsigned observed[16],keyboard_calls;
#define main inherited_hog_main
#include "test_hog_snapshot.c"
#undef main
bool bbp_menu_diag_active(void){return true;}
void bbp_menu_diag_arm(void){}
void bbp_menu_diag_note(uint8_t k,int32_t a,int32_t b,int32_t c,int32_t d){
 (void)a;(void)b;(void)c;(void)d;assert(k<16U);observed[k]++;
}
void bbp_menu_diag_tx(uint8_t k,uint8_t buttons,int ret,unsigned pending){
 (void)buttons;(void)ret;(void)pending;assert(k<16U);observed[k]++;
}
int zmk_hog_send_keyboard_report(struct zmk_hid_keyboard_report_body *body){
 assert(body&&body->modifiers==8);keyboard_calls++;return 73;
}
#undef zmk_hog_send_keyboard_report
int main(void){
 assert(inherited_hog_main()==0);
 assert(observed[MD_ENQUEUE]&&observed[MD_BEGIN]&&observed[MD_NOTIFY]&&observed[MD_CANCEL]);
 struct zmk_hid_keyboard_report_body keyboard={.modifiers=8};
 assert(zmk_hog_send_keyboard_report(&keyboard)==73&&keyboard_calls==1&&observed[MD_KBD]==2);
 puts("PASS diagnostic HOG hooks: all original25 header cases unchanged, enqueue/begin/notify/cancel observed, keyboard body/result passed unchanged to stock function");
 return 0;
}
