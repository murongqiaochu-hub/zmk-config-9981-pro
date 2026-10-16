/* Offline native regression. Only GPIO/kernel/DT are mocked.
 * The THREE included upstream C fixtures below are byte-identical; Python
 * validates all five upstream URL/SHA256 pairs before compiling this TU.
 * DT instance macros intentionally do not expand. PM is not covered.
 * Graph GPIO input returns -EAGAIN for CONTENTION, never an invented raw 0.
 * This file models an electrical boundary, not a substitute scan algorithm.
 */
#include "matrix_isolation_mocks/test_env.h"
#include "matrix_isolation_mocks/kscan_gpio.h"
#include <stdio.h>
#include <string.h>

#include "sdk_v0_3_0_kscan_gpio.c"
#include "matrix_isolation_mocks/sdk_v0_3_0_debounce.c"
#include "sdk_v0_3_0_matrix.c"

#define R 6
#define C 10
#define N (R+C)
enum hypothesis { IDEAL, MIXED_ROW4, NO_DIODES };
enum drive { STRONG_LOW, STRONG_HIGH, HIZ };
/* Both ports are in ONE array: upstream compare_ports() pointer subtraction
 * therefore stays defined C when its qsort comparator sorts the GPIO list. */
static const struct device ports[2] = {{.name="gpio0"}, {.name="gpio1"}};
static struct kscan_gpio inputs[R], outputs[C];
static const unsigned row_port[R] = {1,0,0,1,0,1};
static const unsigned row_pin[R] = {10,6,7,13,12,9};
static const unsigned col_port[C] = {0,1,1,0,1,0,0,0,0,0};
static const unsigned col_pin[C] = {8,2,6,11,11,29,31,30,28,3};
static const unsigned mapped_rc[42][2] = {
 {4,1},{5,1},{4,2},{4,0},{4,5},{5,0},{5,3},
 {0,0},{0,1},{0,2},{0,3},{0,4},{0,5},{0,6},{0,7},{0,8},{0,9},
 {1,0},{1,1},{1,2},{1,3},{1,4},{1,5},{1,6},{1,7},{1,8},{1,9},
 {2,0},{2,1},{2,2},{2,3},{2,4},{2,5},{2,6},{2,7},{2,8},{2,9},
 {3,0},{3,2},{3,4},{3,6},{3,8}
};
static bool closed[R][C], mapped[R][C], logical[C];
static enum drive drives[C];
static enum hypothesis topology;
static bool isolated;
static unsigned port_reads[2], gpio_writes, input_configs, output_configs, callbacks_added;
static unsigned irq_flags[R], conflict_reads, scans, scan_checks, read_ordinal;
static bool verify_native_order;
static int fail_port = -1, fail_set_col = -1, fail_set_value = -1;
static int64_t now;
static struct zmk_debounce_state states[R*C];
static struct kscan_matrix_irq_callback irqs[R];
static struct kscan_matrix_data data;
static struct kscan_matrix_config config;
static struct device matrix = {.name="matrix", .data=&data, .config=&config};
struct event { unsigned row, col; bool pressed; };
static struct event events[4096];
static unsigned event_count;

int64_t k_uptime_get(void) { return now; }
static int find_col(const struct gpio_dt_spec *gpio) {
    for (unsigned c=0;c<C;c++)
        if (gpio->port==&ports[col_port[c]] && gpio->pin==col_pin[c]) return (int)c;
    return -1;
}
static int find_row(const struct gpio_dt_spec *gpio) {
    for (unsigned r=0;r<R;r++)
        if (gpio->port==&ports[row_port[r]] && gpio->pin==row_pin[r]) return (int)r;
    return -1;
}
int gpio_pin_set_dt(const struct gpio_dt_spec *gpio, int value) {
    int c=find_col(gpio); assert(c>=0); assert(value==0 || value==1);
    if (c==fail_set_col && value==fail_set_value) return -EIO;
    gpio_writes++; logical[c]=(bool)value;
    drives[c]=value ? STRONG_HIGH : ((gpio->dt_flags & GPIO_OPEN_SOURCE)==GPIO_OPEN_SOURCE ? HIZ : STRONG_LOW);
    return 0;
}
int gpio_pin_configure_dt(const struct gpio_dt_spec *gpio, gpio_flags_t flags) {
    if (flags==GPIO_INPUT) { assert(find_row(gpio)>=0); input_configs++; }
    else { assert(flags==GPIO_OUTPUT); assert(find_col(gpio)>=0); output_configs++; }
    return 0;
}
int gpio_pin_interrupt_configure_dt(const struct gpio_dt_spec *gpio, gpio_flags_t flags) {
    int r=find_row(gpio); assert(r>=0); irq_flags[r]=flags; return 0;
}
int gpio_add_callback(const struct device *port, struct gpio_callback *cb) {
    assert(port==&ports[0] || port==&ports[1]); assert(cb->handler); callbacks_added++; return 0;
}
static void reach(bool adjacency[N][N], bool reached[N]) {
    bool changed;
    do {
        changed=false;
        for (unsigned a=0;a<N;a++) if(reached[a])
            for (unsigned b=0;b<N;b++) if(adjacency[a][b] && !reached[b]) {
                reached[b]=true; changed=true;
            }
    } while(changed);
}
/* A directed conduction graph, independent of the SDK's scan implementation. */
static int graph_rows(bool high_rows[R]) {
    bool adjacency[N][N]={{false}}, reverse[N][N]={{false}};
    bool high[N]={false}, low_back[N]={false}, uncertain[N]={false};
    for (unsigned r=0;r<R;r++) for(unsigned c=0;c<C;c++) if(closed[r][c]) {
        adjacency[R+c][r]=true;
        if(topology==NO_DIODES || (topology==MIXED_ROW4 && r==4)) adjacency[r][R+c]=true;
    }
    for(unsigned a=0;a<N;a++) for(unsigned b=0;b<N;b++) reverse[b][a]=adjacency[a][b];
    for(unsigned c=0;c<C;c++) { high[R+c]=drives[c]==STRONG_HIGH; low_back[R+c]=drives[c]==STRONG_LOW; }
    reach(adjacency, high); reach(reverse, low_back);
    for(unsigned a=0;a<N;a++) uncertain[a]=high[a] && low_back[a];
    reach(adjacency, uncertain);
    for(unsigned r=0;r<R;r++) {
        if(uncertain[r]) { conflict_reads++; return -EAGAIN; }
        high_rows[r]=high[r];
    }
    return 0;
}
int gpio_port_get(const struct device *port, gpio_port_value_t *value) {
    int p=port==&ports[0]?0:1; assert(port==&ports[p]); port_reads[p]++;
    if(p==fail_port) return -EIO;
    /* During a native scan there must be one and only one selected column.
     * Idle all-high is tested separately without calling the cached getter. */
    unsigned selected=0,selected_col=0;
    for(unsigned c=0;c<C;c++) if(logical[c]) { selected++;selected_col=c; }
    assert(selected==1);
    if(verify_native_order) {
        assert(selected_col==read_ordinal/2); /* original columns 0..9 */
        assert((unsigned)p==read_ordinal%2); /* sorted port0, then port1 */
        read_ordinal++;
    }
    bool rows[R]={false}; int error=graph_rows(rows); if(error) return error;
    *value=0;
    for(unsigned r=0;r<R;r++) if(row_port[r]==(unsigned)p && rows[r]) *value|=BIT(row_pin[r]);
    return 0;
}
static void callback(const struct device *dev, uint32_t r, uint32_t c, bool pressed) {
    assert(dev==&matrix); assert(r<R && c<C); assert(event_count<ARRAY_SIZE(events));
    events[event_count++]=(struct event){r,c,pressed};
}
static void clear_closed(void) { memset(closed,0,sizeof(closed)); }
static void set_key(unsigned index, bool value) { assert(index<42); closed[mapped_rc[index][0]][mapped_rc[index][1]]=value; }
static void assert_outputs(int logical_level, enum drive physical) {
    for(unsigned c=0;c<C;c++) { assert(logical[c]==(bool)logical_level); assert(drives[c]==physical); }
}
static void setup(enum hypothesis hypothesis, bool open_source) {
    topology=hypothesis; isolated=open_source;
    memset(&data,0,sizeof(data)); memset(&config,0,sizeof(config)); memset(states,0,sizeof(states));
    memset(irqs,0,sizeof(irqs)); memset(irq_flags,0,sizeof(irq_flags)); clear_closed();
    memset(logical,0,sizeof(logical)); memset(port_reads,0,sizeof(port_reads));
    event_count=0; input_configs=output_configs=callbacks_added=0; gpio_writes=0;
    fail_port=fail_set_col=fail_set_value=-1; now=1234;
    for(unsigned r=0;r<R;r++) inputs[r]=(struct kscan_gpio){
        .spec={.port=&ports[row_port[r]],.pin=row_pin[r],.dt_flags=GPIO_ACTIVE_HIGH|GPIO_PULL_DOWN},.index=r};
    for(unsigned c=0;c<C;c++) outputs[c]=(struct kscan_gpio){
        .spec={.port=&ports[col_port[c]],.pin=col_pin[c],.dt_flags=open_source?GPIO_OPEN_SOURCE:GPIO_ACTIVE_HIGH},.index=c};
    data.inputs=(struct kscan_gpio_list){inputs,R}; data.irqs=irqs; data.matrix_state=states;
    config=(struct kscan_matrix_config){.outputs={outputs,C},.rows=R,.cols=C,
        .debounce_config={0,0},.debounce_scan_period_ms=1,.poll_period_ms=10,.diode_direction=KSCAN_COL2ROW};
    assert(kscan_matrix_init(&matrix)==0);
    assert(input_configs==R && output_configs==C && callbacks_added==R);
    assert(data.dev==&matrix && data.work.work.handler==kscan_matrix_work_handler);
    for(unsigned i=1;i<R;i++) assert(inputs[i-1].spec.port<=inputs[i].spec.port);
    assert_outputs(0,open_source?HIZ:STRONG_LOW);
    assert(kscan_matrix_configure(&matrix,NULL)==-EINVAL);
    assert(kscan_matrix_api.config(&matrix,callback)==0);
}
static int fresh_native_scan(void) {
    /* This is boundary setup, not a scan: assert native disable sets all
     * columns inactive, then execute the actual unmodified scan body. */
    assert(kscan_matrix_disable(&matrix)==0);
    assert_outputs(0,isolated?HIZ:STRONG_LOW);
    memset(states,0,sizeof(states)); event_count=0; memset(port_reads,0,sizeof(port_reads));
    unsigned writes=gpio_writes; read_ordinal=0;verify_native_order=true;
    int error=kscan_matrix_read(&matrix); scans++;verify_native_order=false;
    if(!error) {
        assert(read_ordinal==2*C);
        assert(port_reads[0]==C && port_reads[1]==C); /* not six port reads/column */
        assert(gpio_writes-writes>=2*C); scan_checks++;
    }
    return error;
}
static void assert_raw(bool expected[R][C]) {
    for(unsigned r=0;r<R;r++) for(unsigned c=0;c<C;c++)
        assert(zmk_debounce_is_pressed(&states[c*R+r])==expected[r][c]);
}
static void test_singles_pairs(void) {
    unsigned singles=0,pairs=0,conflicts[3]={0};
    for(unsigned hypothesis=0;hypothesis<3;hypothesis++) for(unsigned mode=0;mode<2;mode++) {
        setup(hypothesis,mode);
        for(unsigned a=0;a<42;a++) {
            clear_closed();set_key(a,true);assert(fresh_native_scan()==0);assert_raw(closed);singles++;
        }
        for(unsigned a=0;a<42;a++) for(unsigned b=a+1;b<42;b++) {
            clear_closed();set_key(a,true);set_key(b,true);
            bool conflict=mode==0 && mapped_rc[a][0]==mapped_rc[b][0] &&
                (hypothesis==NO_DIODES || (hypothesis==MIXED_ROW4 && mapped_rc[a][0]==4));
            int error=fresh_native_scan();
            if(conflict) { assert(error==-EAGAIN);conflicts[hypothesis]++; }
            else { assert(error==0); assert_raw(closed); }
            pairs++;
        }
    }
    assert(singles==252 && pairs==5166);
    assert(conflicts[IDEAL]==0 && conflicts[MIXED_ROW4]==6 && conflicts[NO_DIODES]==154);
    printf("PASS native matrix/getter: %u single + %u pair scans; legacy CONTENTION errors 0/6/154, HiZ exact; two cached port reads per column\n",singles,pairs);
}
static void test_rectangles(void) {
    unsigned counts[3]={0},triples=0;
    for(unsigned hypothesis=0;hypothesis<3;hypothesis++) {
        setup(hypothesis,true);
        for(unsigned r1=0;r1<R;r1++) for(unsigned r2=r1+1;r2<R;r2++)
        for(unsigned c1=0;c1<C;c1++) for(unsigned c2=c1+1;c2<C;c2++) {
            unsigned corners[4][2]={{r1,c1},{r1,c2},{r2,c1},{r2,c2}};
            if(!mapped[r1][c1] || !mapped[r1][c2] || !mapped[r2][c1] || !mapped[r2][c2]) continue;
            for(unsigned absent=0;absent<4;absent++) {
                clear_closed(); for(unsigned i=0;i<4;i++) if(i!=absent) closed[corners[i][0]][corners[i][1]]=true;
                bool expected[R][C]; memcpy(expected,closed,sizeof(expected));
                bool ghost=hypothesis==NO_DIODES || (hypothesis==MIXED_ROW4 &&
                    (r1==4 || r2==4) && corners[absent][0]!=4);
                if(ghost) { expected[corners[absent][0]][corners[absent][1]]=true;counts[hypothesis]++; }
                assert(fresh_native_scan()==0);assert_raw(expected);triples++;
            }
        }
    }
    assert(triples==2328 && counts[0]==0 && counts[1]==40 && counts[2]==776);
    puts("EXPECTED RISK native: 2328 three-key rectangle scans; ghosts ideal=0/mixed-row4=40/no-diode=776 (unfiltered actual native callbacks)");
    setup(MIXED_ROW4,true);clear_closed();closed[4][1]=closed[4][2]=closed[3][2]=true;
    assert(fresh_native_scan()==0); assert(states[1*R+3].pressed); assert(!mapped[3][1]);
    bool seen=false;
    for(unsigned i=0;i<event_count;i++) if(events[i].row==3 && events[i].col==1 && events[i].pressed) seen=true;
    assert(seen);puts("EXPECTED RISK native: unmapped raw(3,1) ghost also reaches native callback, no keymap filtering");
}
static unsigned count_events(unsigned r,unsigned c,bool pressed) {
    unsigned n=0;for(unsigned i=0;i<event_count;i++) if(events[i].row==r && events[i].col==c && events[i].pressed==pressed)n++;
    return n;
}
static void ticks(unsigned count) {
    for(unsigned i=0;i<count;i++) {
        /* Real read_end() stops fast scheduling once the integrator settles.
         * An idle driver must not be forced to execute invented extra scans. */
        if(!data.work.armed) return;
        now++;
        data.work.armed=false;
        data.work.work.handler(&data.work.work);
    }
}
static void test_timeline_enable_disable_interrupt(void) {
    for(unsigned hypothesis=0;hypothesis<3;hypothesis++) {
        setup(hypothesis,true); config.debounce_config=(struct zmk_debounce_config){10,10};
        assert(kscan_matrix_api.enable_callback(&matrix)==0);
        assert_outputs(1,STRONG_HIGH);assert(!data.work.armed);
        for(unsigned r=0;r<R;r++) assert(irq_flags[r]==GPIO_INT_LEVEL_ACTIVE);
        closed[4][0]=true;
        bool high_rows[R]={false};assert(graph_rows(high_rows)==0 && high_rows[4]);
        assert(irqs[4].callback.handler==kscan_matrix_irq_callback_handler);
        irqs[4].callback.handler(&ports[0],&irqs[4].callback,BIT(12));
        assert_outputs(0,HIZ);assert(data.work.armed && data.work.deadline==K_NO_WAIT);
        for(unsigned r=0;r<R;r++) assert(irq_flags[r]==GPIO_INT_DISABLE);
        ticks(11); assert(states[0*R+4].pressed); assert(count_events(4,0,true)==1);
        for(unsigned n=0;n<12;n++) {
            closed[4][5]=true;ticks(12);closed[4][5]=false;ticks(12);
            closed[4][1]=true;ticks(12);closed[4][1]=false;ticks(12);
            assert(states[4].pressed && count_events(4,0,false)==0);
        }
        assert(count_events(4,5,true)==12 && count_events(4,5,false)==12);
        assert(count_events(4,1,true)==12 && count_events(4,1,false)==12);
        closed[4][1]=closed[4][5]=true;ticks(12);assert(states[4].pressed && states[R+4].pressed && states[5*R+4].pressed);
        clear_closed();ticks(22);
        assert(count_events(4,0,false)==1);assert(!data.work.armed);assert_outputs(1,STRONG_HIGH);
        assert(kscan_matrix_api.disable_callback(&matrix)==0);assert_outputs(0,HIZ);
        assert(!data.work.armed);assert(graph_rows(high_rows)==0);
        for(unsigned r=0;r<R;r++) assert(!high_rows[r] && irq_flags[r]==GPIO_INT_DISABLE);
        /* Re-enable after disable is an actual native scan; do not leave idle
         * all-high columns active while entering the direct scan test helper. */
        assert(kscan_matrix_api.enable_callback(&matrix)==0);assert_outputs(1,STRONG_HIGH);
    }
    puts("PASS native lifecycle: init/sort/config/enable -> idle all1 IRQ -> handler all0/HiZ -> scheduled scan; real 10ms integrator, held MENU + 24 clicks + row4 trio, one final MENU release; disable quiet and re-enable");
}
static void test_getter_cache_errors_and_gpio_failures(void) {
    setup(IDEAL,true);closed[4][0]=true;
    struct kscan_gpio_port_state cache={0};
    assert(gpio_pin_set_dt(&outputs[0].spec,1)==0);
    memset(port_reads,0,sizeof(port_reads));
    for(unsigned i=0;i<R;i++) {
        const struct kscan_gpio *gpio=&inputs[i];
        int actual=kscan_gpio_pin_get(gpio,&cache);assert(actual==(gpio->index==4));
    }
    assert(port_reads[0]==1 && port_reads[1]==1);
    /* Native cache reuses the first snapshot on a port until a different port
     * or a fresh per-column state. Deliberately change switches mid-cache. */
    struct kscan_gpio *row4=NULL;
    for(unsigned i=0;i<R;i++) if(inputs[i].index==4) row4=&inputs[i];
    assert(row4);cache=(struct kscan_gpio_port_state){0};assert(kscan_gpio_pin_get(row4,&cache)==1);
    closed[4][0]=false;assert(kscan_gpio_pin_get(row4,&cache)==1);
    cache=(struct kscan_gpio_port_state){0};assert(kscan_gpio_pin_get(row4,&cache)==0);
    fail_port=0;assert(fresh_native_scan()==-EIO);fail_port=-1;
    /* Upstream early return can leave selected column high. Cleanup belongs
     * to native disable, not to an invented model rollback. */
    assert(logical[0]);assert(kscan_matrix_disable(&matrix)==0);assert_outputs(0,HIZ);
    fail_set_col=0;fail_set_value=1;assert(fresh_native_scan()==-EIO);
    fail_set_col=fail_set_value=-1;assert(kscan_matrix_disable(&matrix)==0);
    puts("PASS native cached getter: sorted ports once/column, snapshot reuse/reset, getter and selected-output errors propagate unchanged; native disable cleanup");
}
int main(void) {
    for(unsigned i=0;i<42;i++) mapped[mapped_rc[i][0]][mapped_rc[i][1]]=true;
    test_singles_pairs();test_rectangles();test_timeline_enable_disable_interrupt();test_getter_cache_errors_and_gpio_failures();
    assert(conflict_reads>=160);
    printf("PASS native total: %u exhaustive direct scans, %u complete cache/column-order checks; no hardware claim\n",scans,scan_checks);
    return 0;
}
