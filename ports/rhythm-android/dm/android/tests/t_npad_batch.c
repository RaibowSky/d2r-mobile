#include "../input_backend.c"
#include "../../host/hle_hid.c"
#include <time.h>
static int source_enabled=1;
static int source_ready(void *p){(void)p;return source_enabled;}
uint64_t now_ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;}
uint64_t guest_ns(void){return now_ns();}
void dm_touch_set(const dm_touch *p,int n){(void)p;(void)n;}
void dm_touch_cancel(void){}
int touch_fill(void *out,uint64_t t){(void)out;(void)t;return 0;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){
    dm_android_input_config c={.source="batch-test",.panel_width=1280,.panel_height=720,.source_ready=source_ready,.logical_npad_id=0,.logical_npad_style=1};
    CHECK(!dm_android_input_bind(&c));CHECK(!dm_android_input_initialize_npad());
    uint32_t id=0;dm_android_npad_state s[16];memset(s,0x7b,sizeof s);
    dm_android_npad_state sentinel=s[1];
    CHECK(!dm_android_input_publish_actions(1,0,0,0,0));CHECK(!dm_android_input_publish_actions(0,0,0,0,0));
    CHECK(hid_GetStates_Handheld(s,16,&id)==0);CHECK(hid_GetStates_JoyDual(s,16,&id)==0);
    CHECK(hid_GetStates_FullKey(s,0,&id)==0);CHECK(hid_GetStates_FullKey(NULL,16,&id)==0);
    CHECK(hid_GetStates_FullKey(s,16,NULL)==0);
    CHECK(hid_GetStates_FullKey(s,16,&id)==1);CHECK(s[0].buttons==1&&s[0].sampling==1&&s[0].attributes==1);
    CHECK(!memcmp(&s[1],&sentinel,sizeof sentinel));
    CHECK(hid_GetStates_FullKey(s,16,&id)==1);CHECK(s[0].buttons==0&&s[0].sampling==2);
    CHECK(!dm_android_input_publish_actions(2,100,-100,0,0));
    CHECK(hid_GetStates_FullKey(s,16,&id)==1);CHECK(s[0].buttons==2&&s[0].lx==100&&s[0].ly==-100);
    CHECK(hid_GetStates_FullKey(s,16,&id)==1&&s[0].buttons==2);
    dm_android_input_cancel();CHECK(hid_GetStates_FullKey(s,16,&id)==1&&s[0].buttons==0);
    CHECK(!dm_android_input_set_styles(2));CHECK(hid_GetStates_FullKey(s,16,&id)==0);
    CHECK(!dm_android_input_set_styles(1));source_enabled=0;CHECK(hid_GetStates_FullKey(s,16,&id)==0);
    puts("PASS Npad batch: one real sample, untouched capacity, short press/release, disconnected style preserves latch, no JoyDual impersonation, held axes, cancel, invalid/zero capacity");
    return 0;
}
