/* Exercise the production mixer; no Android audio stream is opened. */
#include "../../host/hle_audio_renderer.c"
static struct{const void *key;void *value;} objects[128];
void *objmap_get(const void *key){for(unsigned i=0;i<128;i++)if(objects[i].key==key)return objects[i].value;return NULL;}
void objmap_put(const void *key,void *value){for(unsigned i=0;i<128;i++)if(!objects[i].key||objects[i].key==key){objects[i].key=key;objects[i].value=value;return;}abort();}
void hle_signal_system_event(void *event){(void)event;abort();}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)
static Renderer renderer={.m=PTHREAD_MUTEX_INITIALIZER,.rate=48000};
int main(void){
    atomic_store(&g_r,&renderer);ArVoiceHandle v={0},v2={0},pool={0},fm={0},sm={0},sink={0};
    CHECK(h_ar_AcqPool(NULL,&pool,NULL,0)==1);CHECK(h_ar_AcqFm(NULL,&fm,2)==1);CHECK(h_ar_AcqSm(NULL,&sm,48000,4)==1);
    const int8_t output_channels[]={0,1};CHECK(h_ar_AddSink(NULL,&sink,&fm,output_channels,2,"test")==0);
    CHECK(h_ar_AcqVoice(NULL,&v,48000,2,2,0,NULL,0)==1);CHECK(h_ar_AcqVoice(NULL,&v2,48000,1,2,0,NULL,0)==1);CHECK(v.p!=v2.p);
    CHECK(h_ar_VoiceToFm(NULL,&v,&fm)==0);CHECK(h_ar_V2FmVol(&v,&fm,1,0,0)==0);CHECK(h_ar_V2FmVol(&v,&fm,1,1,1)==0);
    // Only frames 1..2 are audible; sentinels must never enter the output.
    const int16_t pcm[]={30000,30000,8192,-8192,16384,-16384,30000,30000};
    ArWaveBuffer w={.buffer=pcm,.bufferSize=sizeof pcm,.startSample=1,.endSample=3};
    CHECK(h_ar_Append(&v,&w)==1);CHECK(h_ar_Append(&v,&w)==1);h_ar_PlayState(&v,0);
    int16_t out[12];CHECK(h_ar_WbCount(&v)==2);mix_into(&renderer,out,1);
    CHECK(h_ar_WbCount(&v)==2);CHECK(abs(out[0]-8192)<=1);CHECK(abs(out[1]+8192)<=1);
    mix_into(&renderer,out,1);CHECK(h_ar_WbCount(&v)==1);CHECK(abs(out[0]-16384)<=1);
    mix_into(&renderer,out,4);CHECK(abs(out[0]-8192)<=1);CHECK(abs(out[2]-16384)<=1);
    CHECK(out[4]==0&&out[5]==0&&out[6]==0&&out[7]==0);CHECK(h_ar_Played(&v)==4);CHECK(h_ar_WbCount(&v)==0);
    h_ar_PlayState(&v,1);CHECK(h_ar_Played(&v)==0);w.loop=1;CHECK(h_ar_Append(&v,&w));h_ar_PlayState(&v,0);
    mix_into(&renderer,out,6);CHECK(h_ar_Played(&v)==6);CHECK(abs(out[10]-16384)<=1);
    h_ar_PlayState(&v,2);mix_into(&renderer,out,6);CHECK(out[0]==0&&h_ar_Played(&v)==6);
    h_ar_PlayState(&v,1);w.loop=0;w.endSample=5;CHECK(h_ar_Append(&v,&w)==0);w.endSample=3;w.startSample=-1;CHECK(h_ar_Append(&v,&w)==0);
    w.startSample=1;
    for(unsigned i=0;i<4;i++)CHECK(h_ar_Append(&v,&w));CHECK(!h_ar_Append(&v,&w));h_ar_PlayState(&v,0);
    mix_into(&renderer,out,1);CHECK(h_ar_WbCount(&v)==4);CHECK(!h_ar_Append(&v,&w));
    h_ar_PlayState(&v,1);CHECK(h_ar_WbCount(&v)==0);
    CHECK(h_ar_RelVoice(NULL,&v)==0);CHECK(!h_ar_Append(&v,&w));
    // Matrix setters must preserve other entries, including explicit zeroes.
    CHECK(h_ar_AcqVoice(NULL,&v,48000,2,2,0,NULL,0));CHECK(h_ar_VoiceToFm(NULL,&v,&fm)==0);
    h_ar_V2FmVol(&v,&fm,1,0,0);h_ar_V2FmVol(&v,&fm,0,1,0);
    CHECK(h_ar_Append(&v,&w));h_ar_PlayState(&v,0);mix_into(&renderer,out,1);
    CHECK(abs(out[0]-8192)<=1&&out[1]==0);h_ar_PlayState(&v,1);
    h_ar_V2FmVol(&v,&fm,0,0,0);h_ar_V2FmVol(&v,&fm,.25f,0,1);h_ar_V2FmVol(&v,&fm,.5f,1,0);
    CHECK(h_ar_Append(&v,&w));h_ar_PlayState(&v,0);mix_into(&renderer,out,1);
    CHECK(abs(out[0]+4096)<=1&&abs(out[1]-2048)<=1);h_ar_PlayState(&v,1);
    // Four-channel submix path + device gain configured before sink creation.
    h_ar_DevVol("test",2);ArVoiceHandle fm2={0},sink2={0};const int8_t swapped[]={1,0};
    CHECK(h_ar_AcqFm(NULL,&fm2,2));CHECK(h_ar_AddSink(NULL,&sink2,&fm2,swapped,2,"test")==0);
    CHECK(h_ar_SmToFm(NULL,&sm,&fm2)==0);h_ar_SmVol(&sm,&fm2,.25f,2,0);h_ar_SmVol(&sm,&fm2,.75f,3,1);
    CHECK(h_ar_VoiceToSm(NULL,&v,&sm)==0);
    h_ar_V2SmVol(&v,&sm,0,0,1);h_ar_V2SmVol(&v,&sm,0,1,0);
    h_ar_V2SmVol(&v,&sm,.5f,0,2);h_ar_V2SmVol(&v,&sm,1,1,3);
    CHECK(h_ar_Append(&v,&w));h_ar_PlayState(&v,0);mix_into(&renderer,out,1);
    CHECK(abs(out[0]+12288)<=1&&abs(out[1]-2048)<=1);h_ar_PlayState(&v,1);h_ar_DevVol("test",1);
    // Effect bypass and dry routing must copy inputs before writing outputs,
    // including crossing/overlapping input/output buses.
    ArVoiceHandle rev={0};CHECK(h_ar_AddRev(NULL,&rev,NULL,0,&sm,2)==0);
    const int8_t fx_in[]={0,1},fx_out[]={2,3};h_ar_RevIo(&rev,fx_in,fx_out,2);
    h_ar_V2SmVol(&v,&sm,1,0,0);h_ar_V2SmVol(&v,&sm,1,1,1);
    CHECK(h_ar_Append(&v,&w));h_ar_PlayState(&v,0);mix_into(&renderer,out,1);
    CHECK(abs(out[0]+6144)<=1&&abs(out[1]-2048)<=1);h_ar_PlayState(&v,1);
    ArReverbParams params={.room=-10000,.reflections_gain=-10000,.reverb_gain=-10000,.decay_time=1,.decay_hf_ratio=1,.diffusion=100,.density=100,.hf_reference=5000,.dry_gain=.5f};h_ar_RevParam(&rev,&params);h_ar_RevOn(&rev,1);
    CHECK(h_ar_Append(&v,&w));h_ar_PlayState(&v,0);mix_into(&renderer,out,1);
    CHECK(abs(out[0]+3072)<=1&&abs(out[1]-1024)<=1);h_ar_PlayState(&v,1);
    // Sum all voices before clipping: .75 + .75 - .75 must remain .75.
    h_ar_RelVoice(NULL,&v);h_ar_RelVoice(NULL,&v2);
    ArVoiceHandle cancel[3]={{0}};const int16_t positive=24576,negative=-24576;
    ArWaveBuffer one={.bufferSize=2,.startSample=0,.endSample=1};
    for(int i=0;i<3;i++){
        CHECK(h_ar_AcqVoice(NULL,&cancel[i],48000,1,2,0,NULL,0));h_ar_VoiceToFm(NULL,&cancel[i],&fm);
        h_ar_V2FmVol(&cancel[i],&fm,1,0,0);h_ar_V2FmVol(&cancel[i],&fm,1,0,1);
        one.buffer=i<2?&positive:&negative;CHECK(h_ar_Append(&cancel[i],&one));h_ar_PlayState(&cancel[i],0);
    }
    mix_into(&renderer,out,1);CHECK(abs(out[0]-24576)<=1&&abs(out[1]-24576)<=1);
    for(int i=0;i<3;i++)h_ar_PlayState(&cancel[i],1);
    one.buffer=&positive;one.loop=1;CHECK(h_ar_Append(&cancel[0],&one));h_ar_PlayState(&cancel[0],0);
    int16_t long_out[481*2];mix_into(&renderer,long_out,481);
    for(int i=0;i<481*2;i++)CHECK(abs(long_out[i]-24576)<=1);
    CHECK(h_ar_Played(&cancel[0])==481&&h_ar_WbCount(&cancel[0])==1);
    uint64_t mixed=atomic_load(&renderer.frames_done),nonzero=atomic_load(&renderer.nonzero_frames);
    dm_audio_renderer_suspend(1);dm_audio_renderer_suspend(1);
    memset(long_out,0x7f,sizeof long_out);mix_into(&renderer,long_out,481);
    for(int i=0;i<481*2;i++)CHECK(long_out[i]==0);
    CHECK(h_ar_Played(&cancel[0])==481&&h_ar_WbCount(&cancel[0])==1);
    CHECK(atomic_load(&renderer.frames_done)==mixed&&atomic_load(&renderer.nonzero_frames)==nonzero);
    dm_audio_renderer_suspend(0);dm_audio_renderer_suspend(0);mix_into(&renderer,long_out,1);
    CHECK(abs(long_out[0]-24576)<=1&&h_ar_Played(&cancel[0])==482);
    CHECK(atomic_load(&renderer.frames_done)==mixed+1);
    h_ar_PlayState(&cancel[0],1);
    puts("PASS renderer lifecycle: background silence, retained queue/voice clock/counters, idempotent suspend/resume, resumed samples");
    puts("PASS production routing: independent matrix entries, channel isolation/crossfeed, four-channel submix, sink byte map, device gain, effect bypass/dry bus routing, sum-before-clip cancellation, multi-chunk callback");
    puts("PASS production PCM mixer: bool acquisition, independent voices, bounded offsets, PCM16 channels, queued clock, loop, pause/stop, four-buffer capacity, active-buffer lifetime");return 0;
}
