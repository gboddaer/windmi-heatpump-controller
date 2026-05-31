#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "../src/spsc_queue.h"

static int test_count = 0;
static int pass_count = 0;
#define TEST(name) do { test_count++; printf("  TEST: %s ...", #name); } while(0)
#define PASS() do { pass_count++; printf(" PASS\n"); } while(0)
#define FAIL(msg) do { printf(" FAIL: %s\n", msg); } while(0)

static void test_push_pop(void) {
    TEST(cmd_push_pop);
    spsc_cmd_t_queue_t q; memset(&q,0,sizeof(q));
    cmd_t c={.type=CMD_SET_DHW_TEMP,.float_val=50.0f,.int_val=0};
    if(!spsc_push_cmd_t(&q,c)){FAIL("push");return;}
    cmd_t o; if(!spsc_pop_cmd_t(&q,&o)){FAIL("pop");return;}
    if(o.type==CMD_SET_DHW_TEMP&&o.float_val==50.0f) PASS(); else FAIL("value");
}

static void test_empty(void) {
    TEST(cmd_empty_pop_false);
    spsc_cmd_t_queue_t q; memset(&q,0,sizeof(q));
    cmd_t o; if(!spsc_pop_cmd_t(&q,&o)) PASS(); else FAIL("");
}

static void test_full(void) {
    TEST(cmd_full_push_false);
    spsc_cmd_t_queue_t q; memset(&q,0,sizeof(q));
    for(int i=0;i<15;i++) if(!spsc_push_cmd_t(&q,(cmd_t){CMD_SET_PRIORITY,0,i})) {FAIL("premature");return;}
    if(!spsc_push_cmd_t(&q,(cmd_t){CMD_SET_DHW_TEMP,0,0})) PASS(); else FAIL("");
}

static void test_fifo(void) {
    TEST(cmd_fifo);
    spsc_cmd_t_queue_t q; memset(&q,0,sizeof(q));
    spsc_push_cmd_t(&q,(cmd_t){CMD_SET_DHW_TEMP,1,0});
    spsc_push_cmd_t(&q,(cmd_t){CMD_SET_HEATING_TEMP,2,0});
    spsc_push_cmd_t(&q,(cmd_t){CMD_SET_PRIORITY,0,3});
    cmd_t o;
    spsc_pop_cmd_t(&q,&o); bool a=(o.type==CMD_SET_DHW_TEMP&&o.float_val==1);
    spsc_pop_cmd_t(&q,&o); bool b=(o.type==CMD_SET_HEATING_TEMP&&o.float_val==2);
    spsc_pop_cmd_t(&q,&o); bool c=(o.type==CMD_SET_PRIORITY&&o.int_val==3);
    if(a&&b&&c) PASS(); else FAIL("");
}

static void test_latest(void) {
    TEST(status_latest);
    spsc_status_snapshot_t_queue_t q; memset(&q,0,sizeof(q));
    spsc_push_status_snapshot_t(&q,(status_snapshot_t){.outdoor_temp=10,.device_online=true});
    spsc_push_status_snapshot_t(&q,(status_snapshot_t){.outdoor_temp=20,.device_online=true});
    spsc_push_status_snapshot_t(&q,(status_snapshot_t){.outdoor_temp=30,.device_online=true});
    status_snapshot_t l; bool f=spsc_latest_status_snapshot_t(&q,&l);
    if(f&&l.outdoor_temp==30.0f) PASS(); else FAIL("");
}

int main(void) {
    printf("=== SPSC Queue Tests ===\n\n");
    test_push_pop(); test_empty(); test_full(); test_fifo(); test_latest();
    printf("\nResults: %d/%d passed\n", pass_count, test_count);
    return (pass_count==test_count)?0:1;
}