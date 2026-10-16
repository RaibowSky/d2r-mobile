/* Exercise production effect on a summed submix, including zero-input tails. */
#include "../../host/hle_audio_renderer.c"
static struct{const void *key;void *value;} objects[128];
void *objmap_get(const void *key){for(int i=0;i<128;i++)if(objects[i].key==key)return objects[i].value;return NULL;}
void objmap_put(const void *key,void *value){for(int i=0;i<128;i++)if(!objects[i].key||objects[i].key==key){objects[i].key=key;objects[i].value=value;return;}abort();}
void hle_signal_system_event(void *event){(void)event;abort();}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x);exit(1);}}while(0)
static Renderer renderer={.m=PTHREAD_MUTEX_INITIALIZER,.rate=48000};
static ArReverbParams preset={.room=-1045,.room_hf=-1000,.decay_time=2.2f,.decay_hf_ratio=.8f,.reflections_gain=-796,.reflections_delay=.0058f,.reverb_gain=-310,.reverb_delay=.05f,.diffusion=80,.density=69,.hf_reference=5000,.dry_gain=.24f};
static int16_t output[48000*2];
static int16_t reference_pcm[48000*2];
int main(int argc,char **argv){
    Renderer *r=&renderer;atomic_store(&g_r,r);
    CHECK(ar_rev_params_valid(&preset));ArReverbParams bad=preset;bad.decay_time=NAN;CHECK(!ar_rev_params_valid(&bad));bad=preset;bad.reflections_delay=.5f;CHECK(!ar_rev_params_valid(&bad));
    ArVoiceHandle fm={0},sm={0},sk={0},rv={0},voice={0};
    CHECK(h_ar_AcqFm(NULL,&fm,2));CHECK(h_ar_AcqSm(NULL,&sm,48000,4));const int8_t map[]={0,1};
    CHECK(!h_ar_AddSink(NULL,&sk,&fm,map,2,"test"));CHECK(!h_ar_SmToFm(NULL,&sm,&fm));h_ar_SmVol(&sm,&fm,1,2,0);h_ar_SmVol(&sm,&fm,1,3,1);
    CHECK(!h_ar_AddRev(NULL,&rv,NULL,0,&sm,2));const int8_t in[]={0,1},out[]={2,3};CHECK(!h_ar_RevIo(&rv,in,out,2));
    CHECK(!h_ar_RevParam(&rv,&preset));CHECK(!h_ar_RevOn(&rv,1));
    CHECK(h_ar_AcqVoice(NULL,&voice,48000,2,2,0,NULL,0));CHECK(!h_ar_VoiceToSm(NULL,&voice,&sm));h_ar_V2SmVol(&voice,&sm,1,0,0);h_ar_V2SmVol(&voice,&sm,1,1,1);
    int16_t impulse[]={16384,0};ArWaveBuffer wb={.buffer=impulse,.bufferSize=sizeof impulse,.endSample=1};CHECK(h_ar_Append(&voice,&wb));h_ar_PlayState(&voice,0);
    mix_into(r,output,48000);CHECK(h_ar_WbCount(&voice)==0);CHECK(h_ar_Played(&voice)==1);
    CHECK(abs(output[0]-3932)<=1);CHECK(output[1]==0);
    unsigned early_nonzero=0,late_nonzero=0,right_nonzero=0;
    for(int f=1;f<48000;f++){
        if(f<250)CHECK(!output[f*2]&&!output[f*2+1]);
        if(f>=250&&f<3000)early_nonzero+=output[f*2]!=0||output[f*2+1]!=0;
        if(f>6000)late_nonzero+=output[f*2]!=0||output[f*2+1]!=0;
        right_nonzero+=output[f*2+1]!=0;
    }
    CHECK(early_nonzero>10&&late_nonzero>100&&right_nonzero>100);
    memcpy(reference_pcm,output,sizeof output);
    ArReverb *effect=objmap_get(rv.p);CHECK(effect->dsp->wet_nonzero>100);
    // Different callback boundaries must produce the same samples and state.
    ar_rev_free(effect->dsp);effect->dsp=ar_rev_create(48000);CHECK(effect->dsp);ar_rev_configure(effect->dsp,&preset);
    h_ar_PlayState(&voice,1);CHECK(h_ar_Append(&voice,&wb));h_ar_PlayState(&voice,0);
    int position=0;const int chunks[]={1,96,240,481,17};
    for(int i=0;position<48000;i++){
        int count=chunks[i%5];if(count>48000-position)count=48000-position;
        mix_into(r,output+position*2,count);position+=count;
    }
    CHECK(!memcmp(output,reference_pcm,sizeof output));
    uint64_t frames=effect->dsp->processed;dm_audio_renderer_suspend(1);mix_into(r,output,48000);
    CHECK(effect->dsp->processed==frames);for(int i=0;i<48000*2;i++)CHECK(!output[i]);
    dm_audio_renderer_suspend(0);mix_into(r,output,1);CHECK(effect->dsp->processed==frames+1);
    CHECK(!h_ar_RevOn(&rv,0));mix_into(r,output,48000);for(int i=0;i<48000*2;i++)CHECK(!output[i]);
    // Cancellation must happen before DSP, rather than processing each voice.
    ar_rev_free(effect->dsp);effect->dsp=ar_rev_create(48000);CHECK(effect->dsp);ar_rev_configure(effect->dsp,&preset);h_ar_RevOn(&rv,1);
    ArVoiceHandle other={0};CHECK(h_ar_AcqVoice(NULL,&other,48000,2,2,0,NULL,0));h_ar_VoiceToSm(NULL,&other,&sm);h_ar_V2SmVol(&other,&sm,1,0,0);h_ar_V2SmVol(&other,&sm,1,1,1);
    int16_t opposite[]={-16384,0};ArWaveBuffer wb2=wb;wb2.buffer=opposite;h_ar_Append(&voice,&wb);h_ar_Append(&other,&wb2);h_ar_PlayState(&other,0);
    mix_into(r,output,48000);for(int i=0;i<48000*2;i++)CHECK(!output[i]);CHECK(!effect->dsp->wet_nonzero);
    ar_rev_free(effect->dsp);effect->dsp=NULL;
    // Full parameter boundaries must remain finite and decay rather than grow.
    for(int pass=0;pass<4;pass++){
        ArReverbParams edge=preset;edge.decay_hf_ratio=pass&1?2:.1f;edge.hf_reference=pass&2?20000:20;edge.density=pass&1?100:0;edge.diffusion=pass&2?0:100;
        ArRevDsp *d=ar_rev_create(48000);CHECK(d);ar_rev_configure(d,&edge);double start_energy=0,end_energy=0;
        for(int f=0;f<48000*5;f++){
            float input[2]={f==0?.25f:0,0},result[2];ar_rev_frame(d,input,result,2);
            CHECK(isfinite(result[0])&&isfinite(result[1]));CHECK(fabsf(result[0])<2&&fabsf(result[1])<2);
            double energy=(double)result[0]*result[0]+(double)result[1]*result[1];
            if(f<48000)start_energy+=energy;if(f>=48000*4)end_energy+=energy;
        }printf("BOUNDARY case=%d start_energy=%.9g end_energy=%.9g\n",pass,start_energy,end_energy);fflush(stdout);CHECK(end_energy<start_energy);ar_rev_free(d);
    }
    // Raw independent-oracle fixtures: channels 1 and 2, impulse + sparse input.
    if(argc>1){FILE *file=fopen(argv[1],"wb");CHECK(file);
        for(int channels=1;channels<=2;channels++){
            ArRevDsp *d=ar_rev_create(48000);CHECK(d);ar_rev_configure(d,&preset);
            for(int f=0;f<48000;f++){
                float input[2]={f==0?.5f:f%997==0?.02f:0,f==13?-.25f:0},result[2]={0};ar_rev_frame(d,input,result,channels);
                CHECK(isfinite(result[0])&&isfinite(result[1]));CHECK(fwrite(result,sizeof(float),2,file)==2);
            }ar_rev_free(d);
        }CHECK(!fclose(file));
    }
    printf("PASS production I3dl2: early=%u late=%u stereo_wet=%u; tail after voice end; exact dry; bypass; pause DSP clock; cancellation before effect; identical callback chunks; HF/density/diffusion boundaries stable and decaying\n",early_nonzero,late_nonzero,right_nonzero);
    return 0;
}
