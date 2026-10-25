/* SPDX-License-Identifier: MIT -- no hardware; observer contract only. */
#include <assert.h>
#include <limits.h>
#include "ipdiag_mock.h"
uint32_t test_now;
struct device test_console;
bool test_dtr;
const struct bt_uuid uuid_report={1},uuid_ref={2};
struct bt_conn test_connections[2]={{0,true,2,65,5},{1,false,1,23,0}};
unsigned test_notify_calls,test_unref_calls;
int test_notify_result;
struct bt_gatt_notify_params *test_seen_params;
void (*test_notify_hook)(void);
static unsigned log_count;
static char last_log[512];
void test_log(const char *fmt, ...) {
    va_list ap;va_start(ap,fmt);vsnprintf(last_log,sizeof(last_log),fmt,ap);va_end(ap);log_count++;
    assert(strstr(last_log,"secret_payload") == NULL);
}
static const struct bt_gatt_attr attrs[26]={
 [5]={NULL,105},[6]={&uuid_report,106},[9]={NULL,109},[10]={&uuid_report,110},
 [13]={NULL,113},[14]={&uuid_report,114},[22]={NULL,122},[23]={&uuid_report,123},
 [8]={&uuid_ref,108},[25]={&uuid_ref,125}
};
const struct bt_gatt_service_static hog_svc={attrs,26};
#include "../config/boards/arm/bbp9981/custom_driver/ipad_diag.c"

static void reuse_epoch(void) { connected(&test_connections[0],0); }
static struct ipdiag_record pop(void) {
    struct ipdiag_record r;assert(ipdiag_pop(&ring,&r));return r;
}
static void clear(void) { struct ipdiag_record r;while(ipdiag_pop(&ring,&r)){} }
int main(void) {
    struct ipdiag_ring q={0};struct ipdiag_record r={0};
    for(unsigned i=0;i<64;i++)assert(ipdiag_push(&q,r));
    assert(!ipdiag_push(&q,r)&&q.dropped==1&&q.sequence==65&&q.count==64);
    for(unsigned i=1;i<=64;i++){assert(ipdiag_pop(&q,&r));assert(r.seq==i);}
    assert(!ipdiag_pop(&q,&r));assert(ipdiag_push(&q,r));assert(ipdiag_pop(&q,&r)&&r.seq==66);
    struct ipdiag_rate rate={0};
    assert(ipdiag_sample(&rate,UINT32_MAX-500U,-1));
    assert(!ipdiag_sample(&rate,100U,-1));assert(ipdiag_sample(&rate,600U,0));
    assert(rate.calls==3&&rate.errors==2);
    connected(&test_connections[0],0);r=pop();assert(r.kind==2&&r.epoch==1&&r.a==1);
    struct bt_gatt_notify_params p={.attr=&attrs[13],.len=9,.data="secret_payload",.user_data=&p};
    test_notify_result=-12;
    assert(bbp_ipdiag_notify(&test_connections[0],&p)==-12);
    assert(test_notify_calls==1&&test_seen_params==&p&&p.attr==&attrs[13]&&p.len==9&&p.user_data==&p);
    r=pop();assert(r.kind==6&&r.a==3&&r.b==114&&r.d==9&&r.e==-12&&r.f==1&&r.g==2);
    for(unsigned i=0;i<500;i++)assert(bbp_ipdiag_notify(&test_connections[0],&p)==-12);
    assert(ring.count==0&&tx[0][3].calls==501&&tx[0][3].errors==501&&suppressed==500);
    test_now=1000;test_notify_result=0;assert(bbp_ipdiag_notify(&test_connections[0],&p)==0);
    r=pop();assert(r.e==0&&r.f==502);
    /* declaration and value map equally; keyboard and GP have independent rates. */
    p.attr=&attrs[5];p.len=8;assert(bbp_ipdiag_notify(&test_connections[0],&p)==0);r=pop();assert(r.a==1&&r.b==106&&r.d==8);
    p.attr=&attrs[23];p.len=9;assert(bbp_ipdiag_notify(&test_connections[0],&p)==0);r=pop();assert(r.a==4&&r.b==123);
    uint8_t ref[]={1,2};
    bbp_ipdiag_read(&test_connections[0],&attrs[8],1,1,ref,2,1,false);
    r=pop();assert(r.kind==5&&r.a==2&&r.d==1&&r.e==1&&r.f==1&&r.g==513);
    bbp_ipdiag_read(&test_connections[0],&attrs[14],64,256,"secret_payload",337,64,true);
    r=pop();assert(r.a==1&&r.d==256&&r.e==64&&r.f==64&&r.g==337);
    bbp_ipdiag_read(&test_connections[0],&attrs[14],64,338,NULL,337,-7,true);r=pop();assert(r.f==-7);
    bbp_ipdiag_read(&test_connections[0],&attrs[14],9,0,"secret_payload",9,9,false);assert(!ring.count);
    snapshot(&test_connections[0],NULL);
    for(unsigned id=1;id<=4;id++) {r=pop();assert(r.kind==4&&r.a==(int)id);assert(r.e==(int)(id==1||id==3));if(id==3)assert(r.f==502&&r.g==501);}
    r=pop();assert(r.kind==3&&r.a==2&&r.b==65&&r.d==5&&r.e==1&&r.f==1&&r.g==1);assert(test_unref_calls==1);
    security_changed(&test_connections[0],1,BT_SECURITY_ERR_UNSPECIFIED);r=pop();assert(r.a==3&&r.b==1&&r.d==1);
    disconnected(&test_connections[0],19);r=pop();assert(r.a==2&&r.b==19&&r.epoch==1);
    connected(&test_connections[0],0);r=pop();assert(r.epoch==2&&tx[0][3].calls==0&&!tx[0][3].seen);
    test_notify_hook=reuse_epoch;
    assert(bbp_ipdiag_notify(&test_connections[0],&p)==0);
    test_notify_hook=NULL;
    r=pop();assert(r.kind==2&&r.epoch==3&&tx[0][4].calls==0&&!ring.count);
    zmk_event_t event=1;assert(input_event(&event)==ZMK_EV_EVENT_BUBBLE&&input_events==1);
    /* CDC closed: no logging regardless of traffic; RAM cannot grow. */
    assert(ipdiag_init()==0);
    for(unsigned i=0;i<1000;i++){test_now+=250;tick(NULL);}
    assert(log_count==0&&ring.count==64&&ring.dropped>0);
    test_dtr=true;unsigned before=ring.count;tick(NULL);assert(log_count<=5&&ring.count>=before-4);
    for(unsigned i=0;i<100;i++){test_now+=250;tick(NULL);}
    assert(ring.count<64&&log_count>0);clear();
    test_dtr=false;unsigned logs=log_count;test_now+=2000;tick(NULL);assert(log_count==logs);
    puts("PASS IPDIAG real runtime: bounded ring/rate wrap, unchanged notify ABI/ret, read privacy, epoch/CCC/security/MTU, DTR gate, late snapshots");
}
