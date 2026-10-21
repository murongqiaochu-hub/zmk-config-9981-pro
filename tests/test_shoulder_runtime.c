#include "test_env.h"
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_scroll_lock.h"
#define DEVICE_DT_NAME(node) "mkp"
#define LCLK 1U
#define RCLK 2U
struct k_mutex { bool locked; };
#define K_MUTEX_DEFINE(name) static struct k_mutex name
static int k_mutex_lock(struct k_mutex *m,int timeout) { (void)timeout;assert(!m->locked);m->locked=true;return 0; }
static int k_mutex_unlock(struct k_mutex *m) { assert(m->locked);m->locked=false;return 0; }
static int32_t atomic_inc(atomic_t *p) { int32_t old=*p;(*p)++;return old; }
static int fail_down, fail_up;
static bool micro_on, micro_blocked, mouse_down, right_mouse_down, fake_mouse_reversed;
static unsigned int toggles, interrupts, replay_count, right_replay_count, busy_true, busy_false;
static bool replay_edges[64], right_replay_edges[64];
static uint32_t replay_times[64], replay_positions[64], right_replay_times[64], right_replay_positions[64];
bool tp_mouse_orientation_reversed(void) { return fake_mouse_reversed; }
void tp_microtap_runtime_toggle(void) { micro_on=!micro_on;toggles++; }
void tp_microtap_runtime_interrupt(void) { interrupts++; }
void tp_microtap_runtime_shoulder_busy(bool busy) {
    micro_blocked=busy;if(busy)busy_true++;else busy_false++;
}
int zmk_behavior_invoke_binding(const struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event,bool pressed) {
    assert(strcmp(binding->behavior_dev,"mkp")==0);
    assert(event.position==TP_LEFT_SHOULDER_POSITION || event.position==TP_RIGHT_SHOULDER_POSITION);
    if(pressed&&fail_down>0) { fail_down--;return -ENODEV; }
    if(!pressed&&fail_up>0) { fail_up--;return -ENODEV; }
    if(binding->param1==LCLK) {
        assert(pressed!=mouse_down);mouse_down=pressed;
        replay_edges[replay_count]=pressed;replay_times[replay_count]=(uint32_t)event.timestamp;
        replay_positions[replay_count++]=event.position;
    } else {
        assert(binding->param1==RCLK&&pressed!=right_mouse_down);right_mouse_down=pressed;
        right_replay_edges[right_replay_count]=pressed;
        right_replay_times[right_replay_count]=(uint32_t)event.timestamp;
        right_replay_positions[right_replay_count++]=event.position;
    }
    return 0;
}
#include "../config/boards/arm/bbp9981/custom_driver/behavior_microtap_shoulder.c"
static void fixture(void) {
    memset(&shoulder,0,sizeof(shoulder));memset(&timeout_work,0,sizeof(timeout_work));
    initialized=false;micro_on=micro_blocked=mouse_down=right_mouse_down=fake_mouse_reversed=false;
    forwarded_down=release_pending=right_forwarded_down=right_release_pending=false;
    release_attempts=right_release_attempts=0;observed_epoch=0;lifecycle_epoch=0;
    left_role_mask=right_role_mask=0;fail_down=fail_up=0;
    toggles=interrupts=replay_count=right_replay_count=busy_true=busy_false=0;
    memset(replay_edges,0,sizeof(replay_edges));memset(right_replay_edges,0,sizeof(right_replay_edges));
    memset(replay_times,0,sizeof(replay_times));memset(replay_positions,0,sizeof(replay_positions));
    memset(right_replay_times,0,sizeof(right_replay_times));memset(right_replay_positions,0,sizeof(right_replay_positions));
    fake_now=0;
}
static void edge_at(uint32_t position,uint32_t t,bool down) {
    fake_now=t;
    struct zmk_behavior_binding binding={0};
    struct zmk_behavior_binding_event event={.position=position,.timestamp=t,.layer=0};
    assert((down?pressed(&binding,event):released(&binding,event))==0);
}
static void edge(uint32_t t,bool down) { edge_at(TP_LEFT_SHOULDER_POSITION,t,down); }
static void timer(uint32_t t) { fake_now=t;tick(&timeout_work.work); }
static void inverted_mouse_swaps_shoulder_button_roles(void) {
    fixture(); fake_mouse_reversed=true;
    edge_at(TP_RIGHT_SHOULDER_POSITION,0,true);
    edge_at(TP_RIGHT_SHOULDER_POSITION,40,false);
    timer(270);
    assert(replay_count==2&&!mouse_down&&!right_mouse_down);
    assert(replay_positions[0]==TP_RIGHT_SHOULDER_POSITION&&replay_positions[1]==TP_RIGHT_SHOULDER_POSITION);

    edge_at(TP_LEFT_SHOULDER_POSITION,300,true);
    assert(right_mouse_down&&right_replay_count==1&&right_replay_edges[0]);
    edge_at(TP_LEFT_SHOULDER_POSITION,340,false);
    assert(!right_mouse_down&&right_replay_count==2&&!right_replay_edges[1]);
    assert(right_replay_positions[0]==TP_LEFT_SHOULDER_POSITION&&
           right_replay_positions[1]==TP_LEFT_SHOULDER_POSITION);

    fixture(); fake_mouse_reversed=true;
    edge_at(TP_RIGHT_SHOULDER_POSITION,0,true);edge_at(TP_RIGHT_SHOULDER_POSITION,40,false);
    edge_at(TP_RIGHT_SHOULDER_POSITION,100,true);edge_at(TP_RIGHT_SHOULDER_POSITION,140,false);
    edge_at(TP_RIGHT_SHOULDER_POSITION,200,true);edge_at(TP_RIGHT_SHOULDER_POSITION,240,false);
    assert(toggles==1&&replay_count==0&&right_replay_count==0);

    /* A held click keeps its press-time owner if orientation changes before release. */
    fixture();
    edge_at(TP_LEFT_SHOULDER_POSITION,0,true);timer(181);
    assert(mouse_down&&replay_count==1);
    fake_mouse_reversed=true;assert(shoulder_lifecycle(NULL)==0);timer(200);
    assert(!mouse_down&&replay_count==2&&!right_mouse_down);
    edge_at(TP_LEFT_SHOULDER_POSITION,250,false);
    assert(replay_count==2&&right_replay_count==0);
}

int main(void) {
    fixture();edge(0,true);edge(50,false);timer(269);assert(replay_count==0&&micro_blocked);
    timer(270);assert(replay_count==2&&replay_edges[0]&&!replay_edges[1]&&!micro_blocked);
    assert(!micro_on&&!mouse_down);

    fixture();edge(0,true);edge(50,false);edge(120,true);edge(160,false);timer(380);
    assert(replay_count==4&&!micro_on&&!mouse_down);
    assert(replay_edges[0]&&!replay_edges[1]&&replay_edges[2]&&!replay_edges[3]);
    assert(replay_times[0]==0&&replay_times[1]==50&&replay_times[2]==120&&replay_times[3]==160);
    /* The new600ms policy is AUTO-only, not an adapter/native cooldown. */
    edge(400,true);edge(430,false);edge(490,true);edge(520,false);timer(740);
    assert(replay_count==8&&!mouse_down&&toggles==0);
    assert(replay_edges[4]&&!replay_edges[5]&&replay_edges[6]&&!replay_edges[7]);
    assert(replay_times[4]==400&&replay_times[5]==430&&replay_times[6]==490&&replay_times[7]==520);

    fixture();edge(0,true);edge(40,false);edge(100,true);edge(140,false);edge(200,true);edge(240,false);
    assert(toggles==1&&micro_on&&replay_count==0&&!mouse_down&&!micro_blocked);
    edge(300,true);edge(340,false);edge(400,true);edge(440,false);edge(500,true);edge(540,false);
    assert(toggles==2&&!micro_on&&replay_count==0);

    fixture();edge(0,true);timer(181);assert(mouse_down&&replay_count==1&&micro_blocked);
    edge(500,false);assert(!mouse_down&&replay_count==2&&!micro_blocked);

    fixture();edge(0,true);tp_microtap_shoulder_motion(30);
    assert(mouse_down&&replay_count==1); /* real drag down precedes XY */
    edge(100,false);assert(!mouse_down&&replay_count==2&&toggles==0);

    fixture();edge(0,true);edge(30,false);tp_microtap_shoulder_motion(50);
    assert(replay_count==2&&!mouse_down&&!micro_blocked); /* click precedes new movement */
    assert(busy_true>0&&busy_false==1&&replay_times[0]==0&&replay_times[1]==30);
    tp_microtap_shoulder_motion(60);assert(replay_count==2&&busy_false==1); /* Idle is not activity. */

    fixture();edge(0,true);edge(30,false);
    zmk_event_t event={.valid=true,.data={.position=41,.state=true,.timestamp=50}};
    fake_now=50;assert(other_key(&event)==0);
    assert(replay_count==2&&toggles==0&&interrupts==2);

    fixture();edge(0,true);edge(30,false);edge(100,true);edge(130,false);edge(200,true);
    timer(381);assert(replay_count==5&&mouse_down&&toggles==0);
    edge(500,false);assert(replay_count==6&&!mouse_down);
    fixture();edge(0,true);edge(30,false);
    assert(shoulder_lifecycle(NULL)==0);timer(60);
    assert(replay_count==0&&!micro_blocked&&toggles==0); /* no delayed click on new host */
    edge(100,true);edge(140,false);edge(200,true);edge(240,false);
    assert(toggles==0);edge(300,true);edge(340,false);assert(toggles==1);

    fixture();edge(0,true);timer(181);assert(mouse_down);
    assert(shoulder_lifecycle(NULL)==0);timer(200);
    assert(!mouse_down&&replay_count==2);edge(300,false);assert(replay_count==2);

    fixture();fail_down=1;edge(0,true);edge(30,false);timer(250);
    assert(replay_count==0&&!mouse_down&&!forwarded_down); /* no orphan UP */
    fixture();fail_up=1;edge(0,true);edge(30,false);timer(250);
    assert(mouse_down&&release_pending);timer(260);
    assert(!mouse_down&&!release_pending&&replay_count==2);

    fixture();
    for(unsigned int n=0;n<3U;n++) {
        struct zmk_behavior_binding binding={0};
        struct zmk_behavior_binding_event e={.position=0,.timestamp=(int64_t)(n*400U),.layer=0};
        fake_now=5000;assert(pressed(&binding,e)==0);e.timestamp+=250;
        assert(released(&binding,e)==0);
    }
    assert(toggles==0&&replay_count==6&&!mouse_down); /* original event time beats delayed dispatch */
    inverted_mouse_swaps_shoulder_button_roles();
    puts("PASS Actual shoulder behavior: stock LCLK/RCLK ownership, inverted role swap, press-time releases, single/double/triple, drag, retry and lifecycle cleanup");
    return 0;
}
