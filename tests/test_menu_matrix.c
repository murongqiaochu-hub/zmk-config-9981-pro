#define CONFIG_BBP9981_MENU_DIAG 1
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include "menu_diag_mocks/kscan_gpio.h"
#include "menu_diag_mocks/zmk/debounce.h"
static uint32_t now;static int level,reads,updates,notes,arms;static bool active=true;
uint32_t k_uptime_get_32(void){return now;}
int kscan_gpio_pin_get(const struct kscan_gpio *g,struct kscan_gpio_port_state *s){assert(g);reads++;s->calls++;return level;}
void zmk_debounce_update(struct zmk_debounce_state *s,bool a,int elapsed,const struct zmk_debounce_config *c){
 assert(elapsed==1&&c->debounce_press_ms==10);updates++;
 s->changed=s->pressed!=a;s->pressed=a;s->counter++;
}
#include "../config/boards/arm/bbp9981/custom_driver/menu_diag_matrix.h"
bool bbp_menu_diag_active(void){return active;}
void bbp_menu_diag_arm(void){active=true;arms++;}
void bbp_menu_diag_note(uint8_t k,int32_t a,int32_t b,int32_t c,int32_t d){
 (void)b;(void)c;(void)d;assert(k==MD_RAW||k==MD_DB);assert(a==0||a==1||a==5);notes++;
}
void bbp_menu_diag_tx(uint8_t k,uint8_t b,int r,unsigned o){(void)k;(void)b;(void)r;(void)o;}
static int probe(size_t row,size_t col,struct kscan_gpio_port_state *cache,struct zmk_debounce_state *state){
 struct kscan_gpio input={.index=row},output={.index=col};
 const struct kscan_gpio *in_gpio=&input,*out_gpio=&output;
 struct zmk_debounce_config cfg={.debounce_press_ms=10,.debounce_release_ms=10};
 int v=kscan_gpio_pin_get(in_gpio,cache);
 if(v>=0)zmk_debounce_update(state,v!=0,1,&cfg);
 return v;
}
int main(void){
 struct kscan_gpio_port_state cache={0};struct zmk_debounce_state state={0};
 level=1;assert(probe(4,0,&cache,&state)==1&&reads==1&&cache.calls==1&&updates==1&&arms==1&&state.pressed);
 int old=notes;assert(probe(1,0,&cache,&state)==1&&notes==old&&reads==2&&updates==2);
 assert(probe(4,7,&cache,&state)==1&&notes==old&&reads==3&&updates==3);
 level=-EIO;old=updates;assert(probe(4,5,&cache,&state)==-EIO&&updates==old&&reads==4);
 level=0;assert(probe(4,1,&cache,&state)==0&&reads==5&&!state.pressed);
 now=1001;assert(probe(4,1,&cache,&state)==0&&reads==6);
 assert(cache.calls==reads);
 puts("PASS matrix prefix: exactly one native cached getter/update call, unchanged arguments/returns/errors, only row4 cols0/1/5 sampled, no GPIO writes or extra reads");
 return 0;
}
