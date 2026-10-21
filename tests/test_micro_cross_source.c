/* Actual runtime + actual shoulder adapter in one TU. Only stock input/bridge
 * operations are mocked; this is not a sensor/SDK/HID or device acceptance test. */
#define main runtime_unit_suite
#define fixture runtime_fixture
#include "test_micro_runtime.c"
#undef fixture
#undef main
#define DEVICE_DT_NAME(node) "mkp"
#define LCLK 1U
#define RCLK 2U
static bool fake_mouse_reversed;
bool tp_mouse_orientation_reversed(void) { return fake_mouse_reversed; }
struct k_mutex { bool locked; };
#define K_MUTEX_DEFINE(name) static struct k_mutex name
static int k_mutex_lock(struct k_mutex *m,int timeout) {
    (void)timeout;assert(!guard_irq_depth&&!m->locked);m->locked=true;return 0;
}
static int k_mutex_unlock(struct k_mutex *m) { assert(m->locked);m->locked=false;return 0; }
static bool native_edges[64];
static uint32_t native_times[64],native_walls[64];
static unsigned int native_count;
static bool native_down;
int zmk_behavior_invoke_binding(const struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event,bool down) {
    assert(!guard_irq_depth&&strcmp(binding->behavior_dev,"mkp")==0&&binding->param1==LCLK);
    assert(event.position==0&&native_count<64&&native_down!=down);
    native_down=down;refs[0]+=down?1:-1;assert(refs[0]>=0);
    native_edges[native_count]=down;native_times[native_count]=(uint32_t)event.timestamp;
    native_walls[native_count++]=fake_now;
    return 0;
}
#define initialized shoulder_initialized
#include "../config/boards/arm/bbp9981/custom_driver/behavior_microtap_shoulder.c"
#undef initialized
static void fixture(void) {
    runtime_fixture(true);memset(&shoulder,0,sizeof(shoulder));
    memset(&timeout_work,0,sizeof(timeout_work));memset(&origin,0,sizeof(origin));memset(&right_origin,0,sizeof(right_origin));
    shoulder_initialized=false;forwarded_down=release_pending=right_forwarded_down=right_release_pending=false;
    release_attempts=right_release_attempts=0;left_role_mask=right_role_mask=0;
    fake_mouse_reversed=false;observed_epoch=0;lifecycle_epoch=0;
    native_count=0;native_down=false;memset(native_edges,0,sizeof(native_edges));
    memset(native_times,0,sizeof(native_times));memset(native_walls,0,sizeof(native_walls));
}
static void edge(uint32_t t,bool down) {
    position_edge(t,0,down);
    struct zmk_behavior_binding binding={0};
    struct zmk_behavior_binding_event event={.position=0,.timestamp=t,.layer=0};
    assert((down?pressed(&binding,event):released(&binding,event))==0);
}
static void timer(uint32_t t) { fake_now=t;tick(&timeout_work.work); }
static void assert_du(unsigned int first,uint32_t down_at,uint32_t up_at) {
    assert(native_edges[first]&&!native_edges[first+1]);
    assert(native_times[first]==down_at&&native_times[first+1]==up_at);
}
int main(void) {
    fixture();edge(0,true);edge(30,false);
    fake_now=50;tp_microtap_shoulder_motion(50);
    assert(native_count==2&&refs[0]==0&&!native_guard.shoulder_busy);
    assert_du(0,0,30);assert(native_walls[0]==50&&native_walls[1]==50);
    /* Exact production ordering: shoulder flush emits D/U in this poll, refs
     * are already zero when the runtime sees the accompanying raw movement. */
    poll_at(50,6,0,true,false);zeros(60,640);
    assert(submissions==0&&moves==1&&refs[0]==0);
    poll_at(649,0,0,true,false);assert(native_guard_observe(false).blocked);
    poll_at(650,8,0,true,false);poll_at(650,8,0,true,false);poll_at(650,8,0,true,false);zeros(660,760);assert(submissions==0);
    poll_at(770,0,0,true,false);assert(submissions==1); /* Exactly 600+120, no queue delay. */
    complete();poll_at(780,0,0,true,false);

    fixture();edge(0,true);edge(50,false);edge(120,true);edge(160,false);timer(380);
    assert(native_count==4&&refs[0]==0);assert_du(0,0,50);assert_du(2,120,160);
    poll_at(380,6,0,true,false);assert(submissions==0&&moves==1);
    /* A second real fast double entirely inside the auto-only 600ms guard is
     * neither filtered nor globally throttled. Original edge times survive. */
    edge(400,true);edge(430,false);edge(490,true);edge(520,false);timer(740);
    assert(native_count==8&&refs[0]==0);assert_du(4,400,430);assert_du(6,490,520);
    poll_at(740,6,0,true,false);assert(submissions==0&&moves==2);

    fixture();edge(10,true);fake_now=20;tp_microtap_shoulder_motion(20);
    assert(native_count==1&&refs[0]==1&&native_down);
    poll_at(20,6,0,true,false);poll_at(30,8,-3,true,false);zeros(40,1000);
    assert(submissions==0&&moves==2&&refs[0]==1); /* Held real drag keeps moving. */
    edge(1010,false);assert(native_count==2&&refs[0]==0);assert_du(0,10,1010);
    poll_at(1010,6,0,true,false);assert(submissions==0&&moves==3);

    fixture();edge(0,true);edge(30,false);poll_at(40,0,0,true,false);
    fake_now=1000;tp_microtap_shoulder_motion(1000);
    assert(native_count==2&&refs[0]==0&&!native_guard.shoulder_busy);
    poll_at(1000,0,0,true,false);
    assert(native_activity_at==1000&&native_guard_observe(false).blocked);
    /* Late flush starts a new window, not the stale position-event clock. */
    zeros(1010,1590);poll_at(1599,0,0,true,false);
    assert(native_guard_observe(false).blocked);poll_at(1600,0,0,true,false);
    assert(!native_guard_observe(false).blocked);
    puts("PASS Actual runtime+shoulder: same-poll flush D/U refs0 blocks auto, 600+120 boundary, fast native DUDUDUDU unchanged inside guard, held drag/move, delayed flush fresh-wall window");
    return 0;
}
