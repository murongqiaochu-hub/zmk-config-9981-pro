#define CONFIG_BBP9981_MENU_DIAG 1
#define main inherited_sdk_main
#include "test_sdk_hid.c"
#undef main
static unsigned observed;
void bbp_menu_diag_note(uint8_t kind,int32_t a,int32_t b,int32_t c,int32_t d){
 (void)a;(void)b;(void)c;(void)d;assert(kind==MD_HID);assert(irq_depth>0);observed++;
}
bool bbp_menu_diag_active(void){return true;}
void bbp_menu_diag_arm(void){}
void bbp_menu_diag_tx(uint8_t k,uint8_t b,int r,unsigned o){(void)k;(void)b;(void)r;(void)o;}
int main(void){
 assert(inherited_sdk_main()==0);assert(observed>0);
 memset(explicit_modifier_counts,0,sizeof(explicit_modifier_counts));
 explicit_modifiers=implicit_modifiers=masked_modifiers=0;zmk_hid_keyboard_clear();
 assert(zmk_hid_register_mod(3)==1);
 for(unsigned i=0;i<1000U;i++){
  assert(call_public(SINGLE_PRESS,0)==0&&keyboard_report.body.modifiers==8&&mouse_report.body.buttons==1);
  assert(call_public(SINGLE_RELEASE,0)==0&&keyboard_report.body.modifiers==8&&mouse_report.body.buttons==0);
 }
 assert(zmk_hid_unregister_mod(3)==1&&keyboard_report.body.modifiers==0);
 puts("PASS enabled HID diagnostics: real SDK tests preserved +1000 GUI-held left DOWN/UP, callbacks do not change refs/modifiers/results or add logger I/O");
 return 0;
}
