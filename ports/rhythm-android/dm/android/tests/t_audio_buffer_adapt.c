/* Induce actual device underruns by stalling only this test's data wrapper.
 * The production control worker and AAudio buffer calls are unchanged. */
#include <aaudio/AAudio.h>
#include <stdatomic.h>
#include <unistd.h>
static _Atomic int slow_callback;
static AAudioStream_dataCallback production_callback;
static aaudio_data_callback_result_t delayed_callback(AAudioStream *s,void *ctx,void *data,int32_t frames){
    if(atomic_load(&slow_callback))usleep(10000);
    return production_callback(s,ctx,data,frames);
}
static void set_test_callback(AAudioStreamBuilder *b,AAudioStream_dataCallback callback,void *ctx){
    production_callback=callback;AAudioStreamBuilder_setDataCallback(b,delayed_callback,ctx);
}
#define AAudioStreamBuilder_setDataCallback set_test_callback
#include "../../host/hle_audio_renderer.c"
#undef AAudioStreamBuilder_setDataCallback
void *objmap_get(const void *key){return (void *)key;}
void hle_signal_system_event(void *event){(void)event;abort();}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x);exit(1);}}while(0)
static Renderer renderer={.m=PTHREAD_MUTEX_INITIALIZER,.rate=48000};
int main(void){
    Renderer *r=&renderer;atomic_store(&g_r,r);ar_out_start(r);usleep(300000);
    int burst=AAudioStream_getFramesPerBurst(g_ostream),initial=AAudioStream_getBufferSizeInFrames(g_ostream);
    CHECK(initial==burst*2);int before=AAudioStream_getXRunCount(g_ostream);
    atomic_store(&slow_callback,1);usleep(450000);atomic_store(&slow_callback,0);usleep(300000);
    int after=AAudioStream_getXRunCount(g_ostream),size=AAudioStream_getBufferSizeInFrames(g_ostream);
    CHECK(after>before);CHECK(size>initial&&size<=burst*8&&size<=AAudioStream_getBufferCapacityInFrames(g_ostream));
    uint64_t frames=atomic_load(&r->frames_done);usleep(200000);CHECK(atomic_load(&r->frames_done)>frames);
    ar_out_stop(r);CHECK(!r->sink_on&&!atomic_load(&r->device_run));
    printf("PASS real underrun adaptation: burst=%d initial=%d adjusted=%d xruns_before=%d xruns_after=%d; callbacks resume and worker joins\n",burst,initial,size,before,after);
    return 0;
}
