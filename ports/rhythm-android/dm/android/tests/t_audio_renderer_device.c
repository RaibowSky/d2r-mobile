/* Real AAudio callback/reopen exercise; error delivery is injected explicitly. */
#include "../../host/hle_audio_renderer.c"
void *objmap_get(const void *key){return (void *)key;}
void hle_signal_system_event(void *event){(void)event;abort();}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x);exit(1);}}while(0)
static Renderer renderer={.m=PTHREAD_MUTEX_INITIALIZER,.rate=48000};
static int16_t source[480*2];
static int wait_progress(uint64_t before){
    for(int i=0;i<100;i++){if(atomic_load(&renderer.frames_done)>before+4800)return 1;usleep(20000);}return 0;
}
int main(void){
    Renderer *r=&renderer;atomic_store(&g_r,r);
    for(int i=0;i<480*2;i++)source[i]=(i&1)?-8192:8192;
    r->sk[0]=(ArSink){.used=1,.count=2,.channels={0,1}};
    r->fm[0]=(ArFinalMix){.used=1,.channels=2,.vol=1,.sink=0};
    ArVoice *v=&r->v[0];v->used=v->play=1;v->rate=48000;v->channels=v->format=2;v->pitch=1;
    v->dest_fm=1;v->dest=&r->fm[0];v->mix[0][0]=v->mix[1][1]=1;
    v->ring[0]=(ArWaveBuffer){.buffer=source,.bufferSize=sizeof source,.endSample=480,.loop=1};v->head=1;
    ar_out_start(r);CHECK(wait_progress(0));
    int burst=AAudioStream_getFramesPerBurst(g_ostream),size=AAudioStream_getBufferSizeInFrames(g_ostream);
    CHECK(burst>0&&size>=burst*2&&size<=AAudioStream_getBufferCapacityInFrames(g_ostream));
    printf("DEVICE_BUFFER burst=%d size=%d capacity=%d xruns=%d\n",burst,size,AAudioStream_getBufferCapacityInFrames(g_ostream),AAudioStream_getXRunCount(g_ostream));
    int64_t device_position=0,device_time=0;int stamp_error=AAUDIO_ERROR_INVALID_STATE;
    for(int i=0;i<100;i++){
        usleep(20000);stamp_error=AAudioStream_getTimestamp(g_ostream,CLOCK_MONOTONIC,&device_position,&device_time);
        if(!stamp_error&&device_position>0)break;
    }
    CHECK(!stamp_error&&device_position>0&&device_time>0);
    for(int pass=0;pass<3;pass++){
        dm_audio_renderer_suspend(1);
        uint64_t frames=atomic_load(&r->frames_done),played=v->played;
        // Trigger the exact production error handler, then let its worker close
        // the real stream. Reopening must be deferred until foreground resume.
        ar_aaudio_error(NULL,r,AAUDIO_ERROR_DISCONNECTED);usleep(250000);
        CHECK(atomic_load(&r->frames_done)==frames);CHECK(v->played==played);CHECK(v->cur&&v->cur_loop);
        dm_audio_renderer_suspend(0);CHECK(wait_progress(frames));
        CHECK(v->played>played);CHECK(atomic_load(&r->nonzero_frames)>0);
    }
    ar_out_stop(r);uint64_t stopped=atomic_load(&r->frames_done);usleep(150000);CHECK(atomic_load(&r->frames_done)==stopped);
    ar_out_start(r);CHECK(wait_progress(stopped));ar_out_stop(r);
    CHECK(!r->sink_on&&!atomic_load(&r->device_run));
    puts("PASS real AAudio: nonzero PCM, three injected error/reopen cycles preserve paused source clocks, stop/restart and worker shutdown");
    return 0;
}
