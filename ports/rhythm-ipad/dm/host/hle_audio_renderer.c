// Software AudioRenderer for this game's nn::audio imports. ABI evidence and
// known limitations are recorded in audio_layout.md. Android output uses AAudio;
// the callback's frame count drives the mixer clock. Buffer changes preserve the
// voice clock, and buffer counts include the currently playing source buffer.
// Voice/submix matrices route PCM into final device channels; persistent I3dl2
// mono/stereo DSP runs after submix accumulation, including zero-input tails.
#include "hle.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <stddef.h>
#include <time.h>
#ifdef __ANDROID__
// Android uses the AAudio callback below; this disables only Apple AudioUnit.
#define AR_NO_AUDIO_OUT 1
#else
#include <AudioToolbox/AudioToolbox.h>
#include <TargetConditionals.h>
#endif

#define R_MAXV    64     // 声部上限（本作的 voiceCount 由 param 给，取小值即可） | voice cap (this game's voiceCount comes from the parameter; a small value is enough)
#define R_MAXSM   16
#define R_MAXFM    8
#define R_MAXSINK  4
#define R_MAXPOOL  8
#define R_MAXREV   4
#define R_RING     4     // SDK AppendWaveBuffer rejects outstanding count == 4.
#define R_BUS_CHANNELS 24
#define R_MIX_CHUNK 240
#define R_RATE 48000
#define R_FRAMES_DEFAULT 240

// ---------- AudioRendererParameter（0x38 字节，只读我们不写） ----------
typedef struct {
    uint32_t sampleRate;     // 0x00（默认 48000）
    uint32_t frameSize;      // 0x04（默认 240 = 5 ms @48 kHz）
    uint32_t u08, u0c;
    int32_t  voiceCount;     // 0x10
    int32_t  u14;
    uint32_t u18a, u18b;     // 0x18 的 8 字节，按两个 u32 声明以免编译器插对齐填充 | the 8 bytes at 0x18, declared as two u32 so the compiler inserts no padding
    uint8_t  u20; uint8_t pad[3];
    uint32_t u24a, u24b;     // 0x24 的 8 字节，同上
    uint32_t u2ca, u2cb;     // 0x2C 的 8 字节，同上
    uint32_t revision;       // 0x34
} ArParam;
_Static_assert(sizeof(ArParam) == 0x38, "AudioRendererParameter is 0x38 bytes (measured)");

// ---------- WaveBuffer (0x30 bytes read; SDK internal ring entries are 0x38) ----------
typedef struct {
    const void *buffer;      // 0x00 PCM 数据地址（guest 指针，宿主可直接读）
    uint64_t bufferSize;     // 0x08 字节数
    int32_t startSample;     // 0x10, inclusive source frame
    int32_t endSample;       // 0x14, exclusive source frame
    uint8_t loop;            // 0x18
    uint8_t endOfStream;     // 0x19
    uint8_t  pad1[6];
    uint64_t u20;            // 0x20 未知（首次追加时记一行日志）
    uint64_t u28;            // 0x28 未知
} ArWaveBuffer;
_Static_assert(sizeof(ArWaveBuffer)==0x30, "WaveBuffer input extent");
_Static_assert(offsetof(ArWaveBuffer,startSample)==0x10, "WaveBuffer start");
_Static_assert(offsetof(ArWaveBuffer,endSample)==0x14, "WaveBuffer end");
_Static_assert(offsetof(ArWaveBuffer,loop)==0x18, "WaveBuffer loop");
_Static_assert(offsetof(ArWaveBuffer,endOfStream)==0x19, "WaveBuffer end of stream");

typedef struct { void *p; } ArVoiceHandle;   // 8 字节（实测）
typedef struct { void *a, *b; } ArHandle;    // 16 字节，按值传（实测）

typedef struct Renderer Renderer;

typedef struct {
    int      used;
    int      play;                 // Host boolean; guest 0=started, 1=stopped, 2=paused.
    int      rate, channels, format;
    float    pitch;
    float    mix[2][R_BUS_CHANNELS]; // source channel -> destination mix channel
    void    *dest;                 // SubMix* 或 FinalMix*（0 = 直连）
    int      dest_fm;
    // 环形：待播 [tail, head)
    ArWaveBuffer ring[R_RING];
    int      head, tail;
    // 当前播放中的缓冲
    const uint8_t *cur; uint64_t cur_frames,cur_start; int cur_ch; int cur_fmt; int cur_loop;
    double   pos;                  // 当前缓冲内的源帧位置（小数用于重采样）
    uint64_t played;               // Rendered frames since stop, across source buffers.
} ArVoice;

typedef struct { int used, channels; float vol; void *dest_fm; float mix[R_BUS_CHANNELS][R_BUS_CHANNELS]; } ArSubMix;
typedef struct { int used, channels; float vol; int sink; } ArFinalMix;
typedef struct { int used, count; int8_t channels[2]; } ArSink;
typedef struct { int used; int attached; void *addr; uint64_t size; } ArPool;
typedef struct {
    float room,room_hf,decay_time,decay_hf_ratio,reflections_gain,reflections_delay;
    float reverb_gain,reverb_delay,diffusion,density,hf_reference,dry_gain;
} ArReverbParams;
_Static_assert(sizeof(ArReverbParams)==0x30,"SDK I3dl2ReverbParameterSet extent");
_Static_assert(offsetof(ArReverbParams,dry_gain)==0x2c,"SDK reverb dry gain");
#include "ar_reverb3d.inc"
typedef struct {
    int used,enabled,count;void *submix;int8_t input[6],output[6];ArReverbParams params;ArRevDsp *dsp;
} ArReverb;

struct Renderer {
    pthread_mutex_t m;
    int      state;                // 0 = 停止 / 1 = 已启动
    int      rate, frames;
    int      sink_on;
    void    *event;                // guest 的 nn::os::SystemEvent（可为 NULL）
    ArVoice  v[R_MAXV];
    ArSubMix sm[R_MAXSM];
    ArFinalMix fm[R_MAXFM];
    ArSink   sk[R_MAXSINK];
    ArPool   pool[R_MAXPOOL];
    ArReverb rev[R_MAXREV];
    float sm_accum[R_MAXSM][R_MIX_CHUNK][R_BUS_CHANNELS];
    int      nrev, npool;
    int      fmt_logged, wb_logged, vol_logged;
    int      suspend_logged;
    // 输出
#ifndef AR_NO_AUDIO_OUT
    AudioUnit au;
#endif
    pthread_t sink_th; _Atomic int sink_run;
#ifdef __ANDROID__
    pthread_t device_th;
    _Atomic int device_run, device_error;
    _Atomic uint64_t callback_ns_max;
#endif
    // 统计（日志用）
    _Atomic uint64_t blk, frames_done, nonzero_frames;
    _Atomic int peak;
};

// 单实例：本作只开一个渲染器。**g_r 永不空**——Open 之前它指向一块静态的惰性实例，于是所有 setter 都可以直接解引用，
// 不必在十几处都写空指针检查（少写一处就是一个崩溃点）。惰性实例里 used 全 0，混音时什么都不做。
// Single instance (this game opens one renderer). g_r is never NULL: before Open it points at a static inert instance, so
// every setter can dereference it without a null check (each missing check would be a crash site). The inert instance has
// every used flag 0, so mixing does nothing.
static Renderer g_r_inert = { .m = PTHREAD_MUTEX_INITIALIZER };
static _Atomic(Renderer *) g_r = &g_r_inert;
// The front-end Surface worker can suspend concurrently with guest Close.
// An atomic pointer alone does not keep its pointee (or its mutex) alive.
static pthread_mutex_t g_ar_lifetime = PTHREAD_MUTEX_INITIALIZER;
// Unlike AudioOut's background drain policy, this rhythm game's renderer must
// retain its source queues and voice clock while the game Surface is suspended.
// Keep callbacks alive but return silence before consuming any source samples.
static _Atomic int g_ar_suspended;
void dm_audio_renderer_suspend(int on){
    on=!!on;
    pthread_mutex_lock(&g_ar_lifetime);
    Renderer *r=atomic_load(&g_r);pthread_mutex_lock(&r->m);
    if(atomic_exchange_explicit(&g_ar_suspended,on,memory_order_acq_rel)!=on){
        LOG("AUDIO_RENDERER_LIFECYCLE requested=%s",on?"suspend":"resume");
        uint64_t played=0;int active=0;
        for(int i=0;i<R_MAXV;i++)if(r->v[i].used){played+=r->v[i].played;active+=r->v[i].play!=0;}
        LOG("AUDIO_RENDERER_LIFECYCLE applied=%s mixed_frames=%llu played_frames=%llu active_voices=%d",
            on?"suspend":"resume",(unsigned long long)atomic_load(&r->frames_done),(unsigned long long)played,active);
        r->suspend_logged=on;
    }
    pthread_mutex_unlock(&r->m);
    pthread_mutex_unlock(&g_ar_lifetime);
}
// Device volume may be configured before OpenAudioRenderer creates its state.
static int g_device_vol_pct=100;

static Renderer *r_of_handle(ArHandle h) {
    Renderer *r = h.a ? objmap_get(h.a) : NULL;
    return r ? r : atomic_load(&g_r);
}
static ArVoice *voice_of(const ArVoiceHandle *vh) {
    if (!vh || !vh->p) return NULL;
    return objmap_get(vh->p);
}
static ArSubMix *sm_of(const ArVoiceHandle *h) { return (h && h->p) ? objmap_get(h->p) : NULL; }
static ArFinalMix *fm_of(const ArVoiceHandle *h) { return (h && h->p) ? objmap_get(h->p) : NULL; }
static ArSink *sk_of(const ArVoiceHandle *h) { return (h && h->p) ? objmap_get(h->p) : NULL; }

// PCM formats are voice properties, not WaveBuffer fields. Acquisition rejects
// unsupported codecs before a buffer reaches this reader.
static inline float rd_sample(const uint8_t *p, uint64_t frame, int ch, int nch, int fmt) {
    size_t i = (size_t)frame * (size_t)nch + (size_t)ch;
    switch (fmt) {
        case 1: return ((const int8_t *)p)[i]/128.0f;
        case 2: return ((const int16_t *)p)[i]/32768.0f;
        case 3: return ((const float *)p)[i];
        default: return 0.0f;
    }
}

// 取下一块待播缓冲；返回 0 = 没有（声部进入停止） | take the next pending buffer; 0 = none (the voice stops)
static int voice_next(ArVoice *v) {
    if (v->tail == v->head) { v->cur = NULL; return 0; }
    const ArWaveBuffer *wb = &v->ring[v->tail & (R_RING - 1)];
    v->tail++;
    v->cur=wb->buffer;v->cur_start=(uint32_t)wb->startSample;v->cur_frames=(uint32_t)wb->endSample;
    v->cur_ch=v->channels;v->cur_fmt=v->format;v->cur_loop=wb->loop!=0;v->pos=(double)v->cur_start;
    return 1;
}

// Direct final-mix voices need no persistent effect state.
static void gains_of(Renderer *r, ArVoice *v, float gains[2][2]) {
    memset(gains,0,4*sizeof(float));
    ArFinalMix *fm=v->dest_fm?objmap_get(v->dest):NULL;
    if(!fm||fm->sink<0||fm->sink>=R_MAXSINK)return;
    ArSink *sk=&r->sk[fm->sink];if(!sk->used)return;
    float scale=fm->vol*(g_device_vol_pct/100.0f);
    for(int src=0;src<v->channels;src++)for(int out=0;out<sk->count;out++){
        gains[src][out]=v->mix[src][sk->channels[out]]*scale;
    }
}
static void mix_submixes(Renderer *r,float sum[R_MIX_CHUNK][2],int frames){
    for(int s=0;s<R_MAXSM;s++){
        ArSubMix *sm=&r->sm[s];if(!sm->used)continue;
        for(int fx=0;fx<r->nrev;fx++){
            ArReverb *rv=&r->rev[fx];if(!rv->used||rv->submix!=sm)continue;
            for(int f=0;f<frames;f++){
                float input[2]={0},output[2]={0};
                for(int c=0;c<rv->count;c++)input[c]=r->sm_accum[s][f][rv->input[c]];
                if(rv->enabled)ar_rev_frame(rv->dsp,input,output,rv->count);
                else memcpy(output,input,sizeof output);
                for(int c=0;c<rv->count;c++)r->sm_accum[s][f][rv->output[c]]=output[c];
            }
        }
        ArFinalMix *fm=sm->dest_fm;
        if(!fm||fm->sink<0||fm->sink>=R_MAXSINK)continue;
        ArSink *sk=&r->sk[fm->sink];if(!sk->used)continue;
        float scale=sm->vol*fm->vol*(g_device_vol_pct/100.0f);
        for(int f=0;f<frames;f++)for(int ch=0;ch<sk->count;ch++)
            for(int bus=0;bus<sm->channels;bus++)sum[f][ch]+=r->sm_accum[s][f][bus]*sm->mix[bus][sk->channels[ch]]*scale;
    }
}

// 混一块：out 为交错双声道 int16 | mix one block; out is interleaved stereo int16
static void mix_into(Renderer *r, int16_t *out, int frames) {
    memset(out, 0, (size_t)frames * 2 * sizeof(int16_t));
    pthread_mutex_lock(&r->m);
    int suspended=atomic_load_explicit(&g_ar_suspended,memory_order_acquire);
    if(suspended!=r->suspend_logged){
        uint64_t played=0;int active=0;
        for(int i=0;i<R_MAXV;i++)if(r->v[i].used){played+=r->v[i].played;active+=r->v[i].play!=0;}
        LOG("AUDIO_RENDERER_LIFECYCLE applied=%s mixed_frames=%llu played_frames=%llu active_voices=%d",
            suspended?"suspend":"resume",(unsigned long long)atomic_load(&r->frames_done),(unsigned long long)played,active);
        r->suspend_logged=suspended;
    }
    if(suspended){pthread_mutex_unlock(&r->m);return;}
    for(int offset=0;offset<frames;offset+=R_MIX_CHUNK){
    int count=frames-offset;if(count>R_MIX_CHUNK)count=R_MIX_CHUNK;
    float sum[R_MIX_CHUNK][2]={{0}};
    for(int s=0;s<R_MAXSM;s++)if(r->sm[s].used)memset(r->sm_accum[s],0,(size_t)count*sizeof r->sm_accum[s][0]);
    for (int i = 0; i < R_MAXV; i++) {
        ArVoice *v = &r->v[i];
        if (!v->used || !v->play) continue;
        // 注意：**音量再小也要继续推进时间线**（played 计数是游戏对时的依据，静音不等于没在播）。
        // Note: even a zero-volume voice must keep advancing its timeline -- the played counter is what the game
        // synchronises against, and silence does not mean "not playing".
        float gains[2][2];gains_of(r,v,gains);
        ArSubMix *sm=!v->dest_fm&&v->dest?objmap_get(v->dest):NULL;
        double step = (double)v->pitch*v->rate/r->rate;
        for (int f = 0; f < count; f++) {
            if (!v->cur) { if (!voice_next(v)) break; }
            if (v->cur && v->cur_frames && (uint64_t)v->pos >= v->cur_frames) {
                if (v->cur_loop) { v->pos=v->cur_start+fmod(v->pos-v->cur_start,(double)(v->cur_frames-v->cur_start)); }
                else { v->cur = NULL; if (!voice_next(v)) break; }
            }
            if (!v->cur) break;
            uint64_t i0 = (uint64_t)v->pos;
            double fr = v->pos - (double)i0;
            uint64_t i1 = i0 + 1; if (i1 >= v->cur_frames) i1 = v->cur_loop ? v->cur_start : i0;
            float l0 = rd_sample(v->cur, i0, 0, v->cur_ch, v->cur_fmt), l1 = rd_sample(v->cur, i1, 0, v->cur_ch, v->cur_fmt);
            float rr0 = v->cur_ch > 1 ? rd_sample(v->cur, i0, 1, v->cur_ch, v->cur_fmt) : l0;
            float rr1 = v->cur_ch > 1 ? rd_sample(v->cur, i1, 1, v->cur_ch, v->cur_fmt) : l1;
            float l = (float)(l0 + (l1 - l0) * fr), rr = (float)(rr0 + (rr1 - rr0) * fr);
            if(sm){
                int s=(int)(sm-r->sm);
                if(s<0||s>=R_MAXSM)die("AudioRenderer invalid submix destination");
                for(int bus=0;bus<sm->channels;bus++)r->sm_accum[s][f][bus]+=l*v->mix[0][bus]+(v->cur_ch>1?rr*v->mix[1][bus]:0);
            }else{
                sum[f][0]+=l*gains[0][0]+(v->cur_ch>1?rr*gains[1][0]:0);
                sum[f][1]+=l*gains[0][1]+(v->cur_ch>1?rr*gains[1][1]:0);
            }
            v->pos += step;
            v->played++; // Source-buffer changes must not reset the rendered sample clock.
            // The guest may recycle a source buffer only once its last frame
            // has been consumed, including when it ends at a callback boundary.
            if(!v->cur_loop && v->pos>=v->cur_frames)v->cur=NULL;
        }
    }
    mix_submixes(r,sum,count);
    // Saturate only after every voice is summed, so temporary overloads can
    // cancel and voice allocation order cannot change the output waveform.
    for(int f=0;f<count;f++)for(int ch=0;ch<2;ch++){
        float a=sum[f][ch];if(a>1)a=1;else if(a< -1)a=-1;
        out[(offset+f)*2+ch]=(int16_t)lroundf(a*32767.0f);
    }
    }
    uint64_t done = atomic_fetch_add(&r->blk, 1) + 1;
    atomic_fetch_add(&r->frames_done, (uint64_t)frames);
    uint64_t nonzero=0;int peak=0;
    for(int f=0;f<frames;f++){int a=abs(out[2*f]),b=abs(out[2*f+1]);if(a||b)nonzero++;if(a>peak)peak=a;if(b>peak)peak=b;}
    atomic_fetch_add(&r->nonzero_frames,nonzero);atomic_store(&r->peak,peak);
    pthread_mutex_unlock(&r->m);
    if (done == 1 || (done % 2000) == 0) LOG("audio::AudioRenderer 混音中：第 %llu 块，累计 %.1f 秒", (unsigned long long)done, atomic_load(&r->frames_done) / (double)r->rate);
    if(done==1||done%2000==0)LOG("AUDIO_PCM_MIX blocks=%llu frames=%llu nonzero_frames=%llu block_peak=%d",(unsigned long long)done,(unsigned long long)atomic_load(&r->frames_done),(unsigned long long)atomic_load(&r->nonzero_frames),peak);
    if(done==1||done%2000==0){
        pthread_mutex_lock(&r->m);
        for(int i=0;i<r->nrev;i++)if(r->rev[i].used){ArRevDsp *d=r->rev[i].dsp;
            LOG("AUDIO_REVERB_DSP effect=%d frames=%llu wet_nonzero_frames=%llu",i,(unsigned long long)d->processed,(unsigned long long)d->wet_nonzero);}
        pthread_mutex_unlock(&r->m);
    }
    // 引擎每块完成时触发一次事件（开关 DM_AR_EVENT_TICK=1）：游戏若按事件等"这一块播完"，默认不触发会等死；
    // 默认只在 RequestUpdate / Start 时触发（SDK 的语义），两种行为都留着，首次真机跑一次就知道该用哪个。
    // Signal once per block when DM_AR_EVENT_TICK=1: a game that waits on the event for "this block finished" would hang
    // otherwise; by default the event is signalled on RequestUpdate / Start (the SDK's semantics). Both behaviours are kept
    // so that one real run tells us which one this game wants.
    { static int tick = -1; if (tick < 0) { const char *e = getenv("DM_AR_EVENT_TICK"); tick = e ? atoi(e) : 0; if (tick) LOG("audio::AudioRenderer：DM_AR_EVENT_TICK=1，每混音块触发一次事件"); }
      if (tick && r->event) hle_signal_system_event(r->event); }
}

// ---------- 输出：静音接收端（两个平台都先用它；Apple 的 CoreAudio 接线在下个分支） ----------
// 静音接收端：每块按设定的帧数、以真实时间推进混音（计数是游戏对时的依据，静音不改变节奏）
// Silent sink: mixes at device pace; the counters stay truthful whether or not anything sounds
static void *ar_sink(void *p) {
    Renderer *r = p;
    int16_t buf[R_FRAMES_DEFAULT * 2 * 4];
    uint64_t t = now_ns();
    while (atomic_load_explicit(&r->sink_run, memory_order_acquire)) {
        int frames = r->frames > 0 ? r->frames : R_FRAMES_DEFAULT;
        if (frames > R_FRAMES_DEFAULT * 4) frames = R_FRAMES_DEFAULT * 4;
        t += (uint64_t)frames * 1000000000ull / (uint64_t)r->rate;
        uint64_t now = now_ns(); if (t > now) usleep((useconds_t)((t - now) / 1000));
        mix_into(r, buf, frames);
    }
    return NULL;
}
#ifdef __ANDROID__
// 输出 = AAudio 低延迟回调：混音按设备回调给的帧数推进 → 计数与耳朵同一个时钟（节奏游戏对时的根据）。
// Android device failures are explicit; disconnections reopen the real stream.
// Output = AAudio's low-latency callback: the mixer advances by the device's frame counts, so the counters share a clock
// with actual output. A silent timer is not accepted as successful device output.
#include <aaudio/AAudio.h>
static AAudioStream *g_ostream;
static pthread_mutex_t g_ar_device_control=PTHREAD_MUTEX_INITIALIZER;
static uint64_t ar_monotonic_ns(void){
    struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint64_t)ts.tv_sec*1000000000ull+(uint64_t)ts.tv_nsec;
}
static aaudio_data_callback_result_t ar_aaudio_cb(AAudioStream *s, void *user, void *audioData, int32_t frames) {
    (void)s; Renderer *r = user;uint64_t begin=ar_monotonic_ns();
    mix_into(r, (int16_t *)audioData, frames);
    uint64_t elapsed=ar_monotonic_ns()-begin,old=atomic_load_explicit(&r->callback_ns_max,memory_order_relaxed);
    while(elapsed>old&&!atomic_compare_exchange_weak_explicit(&r->callback_ns_max,&old,elapsed,memory_order_relaxed,memory_order_relaxed)){}
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
static void ar_aaudio_error(AAudioStream *stream,void *user,aaudio_result_t error){
    (void)stream;Renderer *r=user;
    // The control worker owns close/reopen; never perform them in a callback.
    atomic_store_explicit(&r->device_error,error,memory_order_release);
}
static int ar_device_open(Renderer *r) {
    AAudioStreamBuilder *b = NULL;
    int error=AAudio_createStreamBuilder(&b);
    if(error||!b)return error?error:AAUDIO_ERROR_NO_MEMORY;
    if (b) {
        AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
        AAudioStreamBuilder_setChannelCount(b, 2);
        AAudioStreamBuilder_setSampleRate(b, r->rate);
        AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
        AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
        AAudioStreamBuilder_setUsage(b,AAUDIO_USAGE_GAME);
        AAudioStreamBuilder_setContentType(b,AAUDIO_CONTENT_TYPE_MUSIC);
        AAudioStreamBuilder_setDataCallback(b, ar_aaudio_cb, r);
        AAudioStreamBuilder_setErrorCallback(b,ar_aaudio_error,r);
        { int e = AAudioStreamBuilder_openStream(b, &g_ostream);
          AAudioStreamBuilder_delete(b);
          if(!e&&g_ostream&&(AAudioStream_getSampleRate(g_ostream)!=r->rate||
             AAudioStream_getChannelCount(g_ostream)!=2||AAudioStream_getFormat(g_ostream)!=AAUDIO_FORMAT_PCM_I16))e=AAUDIO_ERROR_INVALID_FORMAT;
          if(!e&&g_ostream){
              int burst=AAudioStream_getFramesPerBurst(g_ostream);
              if(burst<=0||burst>INT32_MAX/8)e=AAUDIO_ERROR_INVALID_FORMAT;
              else{
                  int result=AAudioStream_setBufferSizeInFrames(g_ostream,burst*2);
                  if(result<0)e=result;
                  else LOG("AUDIO_RENDERER_BUFFER burst=%d size=%d capacity=%d target_bursts=2",burst,
                      AAudioStream_getBufferSizeInFrames(g_ostream),AAudioStream_getBufferCapacityInFrames(g_ostream));
              }
          }
          if(!e&&g_ostream)e=AAudioStream_requestStart(g_ostream);
          if (!e && g_ostream) {
              LOG("audio::AudioRenderer 出声：AAudio %d Hz 双声道低延迟，设备回调驱动时间线（每次回调 %d 帧）",
                  r->rate, AAudioStream_getFramesPerDataCallback(g_ostream));
              return 0; }
          if(g_ostream){AAudioStream_close(g_ostream);g_ostream=NULL;}
          return e?e:AAUDIO_ERROR_INVALID_HANDLE;
        }
    }
    return AAUDIO_ERROR_UNAVAILABLE;
}
static void *ar_device_worker(void *user){
    Renderer *r=user;unsigned attempts=0;int last_xruns=0;uint64_t next_metrics=0;
    while(atomic_load_explicit(&r->device_run,memory_order_acquire)){
        int error=atomic_exchange_explicit(&r->device_error,0,memory_order_acq_rel);
        if(!error&&g_ostream&&AAudioStream_getState(g_ostream)==AAUDIO_STREAM_STATE_DISCONNECTED)error=AAUDIO_ERROR_DISCONNECTED;
        if(error){
            LOG("AUDIO_RENDERER_DEVICE disconnected error=%d (%s)",error,AAudio_convertResultToText(error));
            if(g_ostream){AAudioStream_close(g_ostream);g_ostream=NULL;}
        }
        if(!g_ostream&&!atomic_load_explicit(&g_ar_suspended,memory_order_acquire)){
            int e=ar_device_open(r);
            if(e){if(++attempts>=10)die("AudioRenderer real device recovery failed error=%d",e);}
            else {attempts=0;last_xruns=0;next_metrics=0;LOG("AUDIO_RENDERER_DEVICE reopened; source queues and clocks retained");}
        }
        if(g_ostream&&!atomic_load_explicit(&g_ar_suspended,memory_order_acquire)){
            int xruns=AAudioStream_getXRunCount(g_ostream),burst=AAudioStream_getFramesPerBurst(g_ostream);
            int size=AAudioStream_getBufferSizeInFrames(g_ostream);
            int capacity=AAudioStream_getBufferCapacityInFrames(g_ostream),limit=burst*8;
            if(limit>capacity)limit=capacity;
            if(xruns>last_xruns&&size<limit){
                int target=size+burst;if(target>limit)target=limit;
                int result=AAudioStream_setBufferSizeInFrames(g_ostream,target);
                LOG("AUDIO_RENDERER_BUFFER_ADAPT xruns=%d previous=%d size=%d result=%d",xruns,last_xruns,size,result);
            }
            last_xruns=xruns;
            uint64_t now=ar_monotonic_ns();
            if(now>=next_metrics){
                int64_t position=0,time=0;int result=AAudioStream_getTimestamp(g_ostream,CLOCK_MONOTONIC,&position,&time);
                now=ar_monotonic_ns();
                int64_t written=AAudioStream_getFramesWritten(g_ostream);
                // Device timestamp is sampled on the sole stream-control worker,
                // never in the real-time callback. This estimates only the
                // software/device queue, not touch-to-speaker or AV latency.
                double queued=-1;
                if(!result&&time>0&&(int64_t)now>=time){
                    double presented=position+((int64_t)now-time)*(r->rate/1000000000.0);
                    queued=(written-presented)*1000.0/r->rate;
                }
                LOG("AUDIO_RENDERER_TIMING stamp_result=%d written=%lld position=%lld stamp_ns=%lld now_ns=%llu queued_ms=%.3f buffer=%d burst=%d xruns=%d callback_max_us=%.3f",
                    result,(long long)written,(long long)position,(long long)time,(unsigned long long)now,queued,
                    AAudioStream_getBufferSizeInFrames(g_ostream),burst,xruns,
                    atomic_exchange_explicit(&r->callback_ns_max,0,memory_order_relaxed)/1000.0);
                next_metrics=now+1000000000ull;
            }
        }
        usleep(100000);
    }
    if(g_ostream){AAudioStream_requestStop(g_ostream);AAudioStream_close(g_ostream);g_ostream=NULL;}
    return NULL;
}
static void ar_out_start(Renderer *r) {
    pthread_mutex_lock(&g_ar_device_control);
    if(!r->sink_on){
        atomic_store(&r->device_error,0);
        int e=ar_device_open(r);if(e)die("AudioRenderer real device open failed error=%d",e);
        atomic_store_explicit(&r->device_run,1,memory_order_release);
        e=pthread_create(&r->device_th,NULL,ar_device_worker,r);
        if(e){AAudioStream_close(g_ostream);g_ostream=NULL;die("AudioRenderer control worker failed error=%d",e);}
        r->sink_on=1;
    }
    pthread_mutex_unlock(&g_ar_device_control);
}
static void ar_out_stop(Renderer *r) {
    pthread_mutex_lock(&g_ar_device_control);
    if(r->sink_on){
        atomic_store_explicit(&r->device_run,0,memory_order_release);
        pthread_join(r->device_th,NULL);r->sink_on=0;
    }
    pthread_mutex_unlock(&g_ar_device_control);
}
#else   // Apple
static OSStatus ar_au_render(void *ref, AudioUnitRenderActionFlags *fl, const AudioTimeStamp *ts, UInt32 bus, UInt32 frames, AudioBufferList *io) {
    (void)fl; (void)ts; (void)bus;
    Renderer *r = ref;
    mix_into(r, (int16_t *)io->mBuffers[0].mData, (int)frames);
    return noErr;
}
static int ar_real_audio(void) { const char *e = getenv("DM_AUDIO"); return e ? atoi(e) != 0 : TARGET_OS_IPHONE; }

static int ar_au_open(Renderer *r) {
    AudioComponentDescription d = {.componentType = kAudioUnitType_Output,
#if TARGET_OS_IPHONE
                                   .componentSubType = kAudioUnitSubType_RemoteIO,
#else
                                   .componentSubType = kAudioUnitSubType_DefaultOutput,
#endif
                                   .componentManufacturer = kAudioUnitManufacturer_Apple};
    AudioComponent c = AudioComponentFindNext(NULL, &d);
    if (!c || AudioComponentInstanceNew(c, &r->au)) { r->au = NULL; return -1; }
    AudioStreamBasicDescription f = {.mSampleRate = R_RATE, .mFormatID = kAudioFormatLinearPCM,
                                     .mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked,
                                     .mBytesPerPacket = 4, .mFramesPerPacket = 1, .mBytesPerFrame = 4,
                                     .mChannelsPerFrame = 2, .mBitsPerChannel = 16};
    AURenderCallbackStruct cb = {ar_au_render, r};
    if (AudioUnitSetProperty(r->au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f) ||
        AudioUnitSetProperty(r->au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb) ||
        AudioUnitInitialize(r->au)) { AudioComponentInstanceDispose(r->au); r->au = NULL; return -1; }
    return 0;
}
static void ar_out_start(Renderer *r) {
    if (r->sink_on) return;
    if (ar_real_audio() && !r->au) ar_au_open(r);
    if (r->au && !AudioOutputUnitStart(r->au)) { r->sink_on = 1; LOG("audio::AudioRenderer 出声：CoreAudio %d Hz 双声道（%d 帧一块）", r->rate, r->frames); return; }
    if (r->au) { AudioUnitUninitialize(r->au); AudioComponentInstanceDispose(r->au); r->au = NULL; }
    die("iPad AudioRenderer real CoreAudio output failed");
    atomic_store_explicit(&r->sink_run, 1, memory_order_release);
    pthread_create(&r->sink_th, NULL, ar_sink, r);
    r->sink_on = 1;
    LOG("audio::AudioRenderer：没有输出设备或 DM_AUDIO=0 → 静音接收端按真实时间消耗（不出声，节奏与真机一致）");
}
static void ar_out_stop(Renderer *r) {
    if (!r->sink_on) return;
    if (r->au) AudioOutputUnitStop(r->au);
    else { atomic_store_explicit(&r->sink_run, 0, memory_order_release); pthread_join(r->sink_th, NULL); }
    r->sink_on = 0;
}
#endif   // AR_NO_AUDIO_OUT

// ---------- 初始化 / 生命周期 ----------
HLE_FORCE("_ZN2nn5audio32InitializeAudioRendererParameterEPNS0_22AudioRendererParameterE",
          "AudioRenderer 参数默认值（0x38 字节：48000 / 240 / 各计数 1）", void, h_ar_InitParam, ArParam *p) {
    memset(p, 0, sizeof *p);
    p->sampleRate = R_RATE; p->frameSize = R_FRAMES_DEFAULT; p->u08 = 1; p->voiceCount = 1; p->u14 = 1; p->revision = 1;
}
// 工作缓冲大小：宿主不使用这块缓冲（宿主状态在自己的堆上），给一个随声部数增长的合理值即可。
// Work buffer size: the host does not use this buffer (its state is on its own heap); a plausible value growing with the
// voice count is enough. It only has to be large enough that the game's own sanity checks pass.
HLE_FORCE("_ZN2nn5audio30GetAudioRendererWorkBufferSizeERKNS0_22AudioRendererParameterE",
          "工作缓冲大小；宿主不用这块缓冲，给合理值", uint64_t, h_ar_WbSize, const ArParam *p) {
    int vc = p && p->voiceCount > 0 ? p->voiceCount : 1;
    return (uint64_t)0x10000 + (uint64_t)vc * 0x1000;
}
HLE_FORCE("_ZN2nn5audio36GetAudioRendererConfigWorkBufferSizeERKNS0_22AudioRendererParameterE",
          "配置工作缓冲大小；同上", uint64_t, h_ar_CfgWbSize, const ArParam *p) { (void)p; return 0x4000; }
HLE_FORCE("_ZN2nn5audio35GetRequiredBufferSizeForI3dl2ReverbEii",
          "混响延遲線由宿主於AddEffect配置；不使用guest工作緩衝", uint64_t, h_ar_RevSize, int a, int b) { (void)a; (void)b; return 0; }

HLE_FORCE("_ZN2nn5audio29InitializeAudioRendererConfigEPNS0_19AudioRendererConfigERKNS0_22AudioRendererParameterEPvm",
          "初始化配置：宿主不往 guest 的 config 里写任何东西（用 objmap 关联宿主状态）", Result, h_ar_InitConfig,
          void *cfg, const ArParam *p, void *work, uint64_t size) {
    (void)work; (void)size;
    Renderer *r = atomic_load(&g_r);
    if (!r) return RES_OK;
    pthread_mutex_lock(&r->m);
    if (p) { r->rate = p->sampleRate ? (int)p->sampleRate : R_RATE; r->frames = p->frameSize ? (int)p->frameSize : R_FRAMES_DEFAULT; }
    pthread_mutex_unlock(&r->m);
    objmap_put(cfg, cfg);   // config 指针 → 宿主状态（值不重要，键存在即可；见 r_of_config）
    LOG("audio::InitializeAudioRendererConfig cfg=%p param=%p work=%p size=%llu（宿主不写 guest 的 config）",
        cfg, (const void *)p, work, (unsigned long long)size);
    return RES_OK;
}
HLE_FORCE("_ZN2nn5audio17OpenAudioRendererEPNS0_19AudioRendererHandleEPNS_2os11SystemEventERKNS0_22AudioRendererParameterEPvm",
          "打开渲染器：建宿主对象，handle 写两个宿主指针（16 字节，实测）", Result, h_ar_Open,
          void *out, void *event, const ArParam *p, void *work, uint64_t size) {
    Renderer *r = calloc(1, sizeof *r);
    if (!r) return RES_FAIL;
    pthread_mutex_init(&r->m, NULL);
    r->rate = (p && p->sampleRate) ? (int)p->sampleRate : R_RATE;
    r->frames = (p && p->frameSize) ? (int)p->frameSize : R_FRAMES_DEFAULT;
    if (r->frames <= 0 || r->frames > R_FRAMES_DEFAULT * 8) { LOG("【红】AudioRenderer frameSize=%d 不合理，按 %d", r->frames, R_FRAMES_DEFAULT); r->frames = R_FRAMES_DEFAULT; }
    if (r->rate != R_RATE) LOG("【红】AudioRenderer sampleRate=%d（只做 %d，输出仍按 %d）", r->rate, R_RATE, R_RATE);
    r->event = event;
    for (int i = 0; i < R_MAXFM; i++) r->fm[i].sink = -1;
    atomic_store(&g_r, r);
    if (r->event) hle_bind_system_event(r->event, 1);
    if (out) { ArHandle h; h.a = r; h.b = r; memcpy(out, &h, sizeof h); }
    LOG("audio::OpenAudioRenderer handle←{%p,%p} param(sampleRate=%d frameSize=%d voiceCount=%d) work=%p size=%llu event=%p",
        (void *)r, (void *)r, r->rate, r->frames, (int)(p ? p->voiceCount : 0), work, (unsigned long long)size, event);
    return RES_OK;
}
HLE_FORCE("_ZN2nn5audio18StartAudioRendererENS0_19AudioRendererHandleE", "开始渲染", Result, h_ar_Start, ArHandle h) {
    Renderer *r = r_of_handle(h); if (!r) return RES_FAIL;
    atomic_store(&g_r, r); r->state = 1; ar_out_start(r);
    if (r->event) hle_signal_system_event(r->event);
    LOG("audio::StartAudioRenderer");
    return RES_OK;
}
HLE_FORCE("_ZN2nn5audio17StopAudioRendererENS0_19AudioRendererHandleE", "停止渲染", Result, h_ar_Stop, ArHandle h) {
    Renderer *r = r_of_handle(h); if (!r) return RES_FAIL;
    r->state = 0; ar_out_stop(r); LOG("audio::StopAudioRenderer"); return RES_OK;
}
HLE_FORCE("_ZN2nn5audio18CloseAudioRendererENS0_19AudioRendererHandleE", "关闭渲染器：停输出、释放", Result, h_ar_Close, ArHandle h) {
    Renderer *r = r_of_handle(h); if (!r) return RES_FAIL;
    ar_out_stop(r);
    pthread_mutex_lock(&g_ar_lifetime);
    for(int i=0;i<r->nrev;i++)ar_rev_free(r->rev[i].dsp);
#ifndef AR_NO_AUDIO_OUT
    if (r->au) { AudioUnitUninitialize(r->au); AudioComponentInstanceDispose(r->au); r->au = NULL; }
#endif
    if (atomic_load(&g_r) == r) atomic_store(&g_r, &g_r_inert);   // 回到惰性实例，不停在已释放的指针上 | back to the inert instance, never leave a freed pointer behind
    pthread_mutex_destroy(&r->m); free(r);
    pthread_mutex_unlock(&g_ar_lifetime);
    LOG("audio::CloseAudioRenderer"); return RES_OK;
}
HLE_FORCE("_ZN2nn5audio26RequestUpdateAudioRendererENS0_19AudioRendererHandleEPKNS0_19AudioRendererConfigE",
          "请求一次更新：宿主每块都在实时混音，这里只触发事件并把配置关联上", Result, h_ar_Update,
          ArHandle h, const void *cfg) {
    Renderer *r = r_of_handle(h); if (!r) return RES_FAIL;
    if (cfg && !objmap_get(cfg)) objmap_put((void *)cfg, (void *)cfg);
    if (r->event) hle_signal_system_event(r->event);
#ifdef AR_NO_AUDIO_OUT
    if (!r->sink_on && r->state) ar_out_start(r);   // 万一 Start 之后输出被停掉 | in case the output was stopped after Start
#else
    if (!r->au && !r->sink_on && r->state) ar_out_start(r);
#endif
    return RES_OK;
}

// ---------- 内存池 ----------
HLE_FORCE("_ZN2nn5audio17AcquireMemoryPoolEPNS0_19AudioRendererConfigEPNS0_14MemoryPoolTypeEPvm",
          "取得内存池句柄，SDK bool success", unsigned, h_ar_AcqPool, void *cfg, void *out, void *addr, uint64_t size) {
    (void)cfg;
    Renderer *r = atomic_load(&g_r); if (!r || !out) return 0;
    if (r->npool >= R_MAXPOOL) return 0;
    ArPool *p = &r->pool[r->npool++];
    p->used = 1; p->attached = 0; p->addr = addr; p->size = size;
    *(void **)out = p; objmap_put(p, p);
    LOG("audio::AcquireMemoryPool addr=%p size=%llu", addr, (unsigned long long)size);
    return 1;
}
HLE_FORCE("_ZN2nn5audio23RequestAttachMemoryPoolEPNS0_14MemoryPoolTypeE", "挂上内存池", Result, h_ar_Attach, void *h) {
    ArPool *p = (h && *(void **)h) ? objmap_get(*(void **)h) : NULL; if (!p) return RES_FAIL; p->attached = 1; return RES_OK;
}
HLE_FORCE("_ZN2nn5audio23RequestDetachMemoryPoolEPNS0_14MemoryPoolTypeE", "摘掉内存池", Result, h_ar_Detach, void *h) {
    ArPool *p = (h && *(void **)h) ? objmap_get(*(void **)h) : NULL; if (!p) return RES_FAIL; p->attached = 0; return RES_OK;
}
HLE_FORCE("_ZN2nn5audio20IsMemoryPoolAttachedEPKNS0_14MemoryPoolTypeE", "内存池是否挂着（bool）", unsigned, h_ar_Attached, const void *h) {
    ArPool *p = (h && *(void *const *)h) ? objmap_get(*(void *const *)h) : NULL; return p && p->attached ? 1u : 0u;
}

// ---------- 图的构建 ----------
HLE_FORCE("_ZN2nn5audio13AcquireSubMixEPNS0_19AudioRendererConfigEPNS0_10SubMixTypeEii", "取得子混音，SDK bool success", unsigned, h_ar_AcqSm,
          void *cfg, void *out, int rate, int channels) {
    (void)cfg;
    Renderer *r = atomic_load(&g_r); if (!r || !out) return 0;
    if(channels<1||channels>R_BUS_CHANNELS)return 0;
    if(rate!=r->rate)die("AudioRenderer submix resampling unsupported rate=%d renderer=%d",rate,r->rate);
    for (int i = 0; i < R_MAXSM; i++) if (!r->sm[i].used) {
        r->sm[i].used = 1; r->sm[i].vol = 1.0f; r->sm[i].dest_fm = NULL;
        r->sm[i].channels=channels;memset(r->sm[i].mix,0,sizeof r->sm[i].mix);
        *(void **)out = &r->sm[i]; objmap_put(&r->sm[i], &r->sm[i]);
        LOG("audio::AcquireSubMix idx=%d rate=%d channels=%d",i,rate,channels);
        return 1;
    }
    return 0;
}
HLE_FORCE("_ZN2nn5audio15AcquireFinalMixEPNS0_19AudioRendererConfigEPNS0_12FinalMixTypeEi", "取得最终混音，SDK bool success", unsigned, h_ar_AcqFm,
          void *cfg, void *out, int channels) {
    (void)cfg;
    Renderer *r = atomic_load(&g_r); if (!r || !out) return 0;
    if(channels<1||channels>R_BUS_CHANNELS)return 0;
    for (int i = 0; i < R_MAXFM; i++) if (!r->fm[i].used) {
        r->fm[i].used = 1; r->fm[i].vol = 1.0f; r->fm[i].sink = -1;
        r->fm[i].channels=channels;
        *(void **)out = &r->fm[i]; objmap_put(&r->fm[i], &r->fm[i]);
        LOG("audio::AcquireFinalMix idx=%d channels=%d",i,channels);
        return 1;
    }
    return 0;
}
HLE_FORCE("_ZN2nn5audio13AddDeviceSinkEPNS0_19AudioRendererConfigEPNS0_14DeviceSinkTypeEPNS0_12FinalMixTypeEPKaiPKc",
          "加设备输出端；宿主只有一个设备（默认输出）", Result, h_ar_AddSink,
          void *cfg, void *out, const void *fmh, const int8_t *ints, int n, const char *name) {
    (void)cfg;
    Renderer *r = atomic_load(&g_r); if (!r || !out) return RES_FAIL;
    ArFinalMix *fm = (fmh && *(void *const *)fmh) ? objmap_get(*(void *const *)fmh) : NULL;
    if(!fm||!ints||n<1||n>2)return RES_FAIL;
    for(int ch=0;ch<n;ch++)if(ints[ch]<0||ints[ch]>=fm->channels)return RES_FAIL;
    for (int i = 0; i < R_MAXSINK; i++) if (!r->sk[i].used) {
        r->sk[i].used = 1;r->sk[i].count=n;memcpy(r->sk[i].channels,ints,(size_t)n);
        if (fm) fm->sink = i;
        *(void **)out = &r->sk[i]; objmap_put(&r->sk[i], &r->sk[i]);
        LOG("audio::AddDeviceSink idx=%d finalMix=%p channels=%d,%d n=%d name=%s",i,(void *)fm,ints[0],n>1?ints[1]:-1,n,name?name:"(null)");
        return RES_OK;
    }
    return RES_FAIL;
}
HLE_FORCE("_ZN2nn5audio14AddI3dl2ReverbEPNS0_19AudioRendererConfigEPNS0_15I3dl2ReverbTypeEPvmPNS0_10SubMixTypeEi",
          "配置持續I3dl2 mono/stereo DSP與延遲線", Result, h_ar_AddRev,
          void *cfg, void *out, void *buf, uint64_t size, const void *smh, int n) {
    (void)cfg; (void)buf; (void)size;
    Renderer *r = atomic_load(&g_r); if (!r || !out) return RES_FAIL;
    if (r->nrev >= R_MAXREV||n<1||n>2||!sm_of(smh)) return RES_FAIL;
    ArRevDsp *dsp=ar_rev_create((uint32_t)r->rate);if(!dsp)return RES_FAIL;
    pthread_mutex_lock(&r->m);
    ArReverb *rv = &r->rev[r->nrev++];
    rv->used = 1; rv->enabled = 0;rv->submix=sm_of(smh);rv->dsp=dsp;
    rv->params=(ArReverbParams){.room=-10000,.reflections_gain=-10000,.reverb_gain=-10000,.decay_time=1,.decay_hf_ratio=1,.diffusion=100,.density=100,.hf_reference=5000,.dry_gain=1};
    ar_rev_configure(dsp,&rv->params);
    pthread_mutex_unlock(&r->m);
    *(void **)out = rv; objmap_put(rv, rv);
    LOG("audio::AddI3dl2Reverb idx=%d wet_tail_implemented=true float_reference=Ryujinx",r->nrev-1);
    return RES_OK;
}
HLE_FORCE("_ZN2nn5audio19SetVoiceDestinationEPNS0_19AudioRendererConfigEPNS0_9VoiceTypeEPNS0_10SubMixTypeE",
          "声部 → 子混音", Result, h_ar_VoiceToSm, void *cfg, const ArVoiceHandle *v, const ArVoiceHandle *sm) {
    (void)cfg;
    Renderer *r = atomic_load(&g_r);
    ArVoice *vo = voice_of(v); if (!r || !vo) return RES_FAIL;
    ArSubMix *s = sm_of(sm);
    pthread_mutex_lock(&r->m);
    vo->dest = s; vo->dest_fm = 0;
    pthread_mutex_unlock(&r->m);
    return RES_OK;
}
HLE_FORCE("_ZN2nn5audio19SetVoiceDestinationEPNS0_19AudioRendererConfigEPNS0_9VoiceTypeEPNS0_12FinalMixTypeE",
          "声部 → 最终混音（直连）", Result, h_ar_VoiceToFm, void *cfg, const ArVoiceHandle *v, const ArVoiceHandle *fm) {
    (void)cfg;
    Renderer *r = atomic_load(&g_r);
    ArVoice *vo = voice_of(v); if (!r || !vo) return RES_FAIL;
    ArFinalMix *f = fm_of(fm);
    pthread_mutex_lock(&r->m);
    vo->dest = f; vo->dest_fm = 1;
    pthread_mutex_unlock(&r->m);
    return RES_OK;
}
HLE_FORCE("_ZN2nn5audio20SetSubMixDestinationEPNS0_19AudioRendererConfigEPNS0_10SubMixTypeEPNS0_12FinalMixTypeE",
          "子混音 → 最终混音", Result, h_ar_SmToFm, void *cfg, const ArVoiceHandle *sm, const ArVoiceHandle *fm) {
    (void)cfg;
    ArSubMix *s = sm_of(sm); if (!s) return RES_FAIL;
    Renderer *r=atomic_load(&g_r);pthread_mutex_lock(&r->m);
    s->dest_fm = fm_of(fm);
    pthread_mutex_unlock(&r->m);
    return RES_OK;
}
HLE_FORCE("_ZN2nn5audio16AcquireVoiceSlotEPNS0_19AudioRendererConfigEPNS0_9VoiceTypeEiiNS0_12SampleFormatEiPKvm",
          "取得声部，sample rate / channels / format / priority; SDK bool success", unsigned, h_ar_AcqVoice,
          void *cfg, void *out, int rate, int channels, int fmt, int priority, const void *data, uint64_t size) {
    (void)cfg;
    Renderer *r = atomic_load(&g_r); if (!r || !out) return 0;
    if(rate<8000||rate>192000||channels<1||channels>2)return 0;
    if(fmt<1||fmt>3)die("AudioRenderer unsupported source format %d",fmt);
    if(data||size)die("AudioRenderer unexpected PCM codec context");
    pthread_mutex_lock(&r->m);
    int slot=0;while(slot<R_MAXV&&r->v[slot].used)slot++;
    if(slot==R_MAXV){pthread_mutex_unlock(&r->m);return 0;}
    ArVoice *v = &r->v[slot];
    memset(v, 0, sizeof *v);
    v->used = 1; v->play = 0; v->pitch = 1.0f; v->head = v->tail = 0;
    v->rate=rate;v->channels=channels;v->format=fmt;
    *(void **)out = v; objmap_put(v, v);
    pthread_mutex_unlock(&r->m);
    if (!r->fmt_logged) { r->fmt_logged = 1; LOG("audio::AcquireVoiceSlot slot=%d rate=%d channels=%d fmt=%d priority=%d",slot,rate,channels,fmt,priority); }
    return 1;
}
HLE_FORCE("_ZN2nn5audio16ReleaseVoiceSlotEPNS0_19AudioRendererConfigEPNS0_9VoiceTypeE", "释放声部", Result, h_ar_RelVoice,
          void *cfg, const ArVoiceHandle *v) {
    (void)cfg;
    ArVoice *vo = voice_of(v); if (!vo) return RES_FAIL;
    Renderer *r = atomic_load(&g_r);
    pthread_mutex_lock(&r->m); memset(vo, 0, sizeof *vo); pthread_mutex_unlock(&r->m);
    return RES_OK;
}

// ---------- 运行时控制 ----------
HLE_FORCE("_ZN2nn5audio17SetVoicePlayStateEPNS0_9VoiceTypeENS1_9PlayStateE", "声部播放状态（0 started, 1 stopped, 2 paused）", Result, h_ar_PlayState,
          const ArVoiceHandle *v, int st) {
    ArVoice *vo = voice_of(v); if (!vo) return RES_FAIL;
    Renderer *r = atomic_load(&g_r);
    if(st<0||st>2)die("AudioRenderer invalid play state %d",st);
    pthread_mutex_lock(&r->m); vo->play=st==0;
    if(st==1){vo->cur=NULL;vo->head=vo->tail=0;vo->pos=0;vo->played=0;}
    pthread_mutex_unlock(&r->m);
    return RES_OK;
}
HLE_FORCE("_ZN2nn5audio13SetVoicePitchEPNS0_9VoiceTypeEf", "声部音高（重采样比）", Result, h_ar_Pitch, const ArVoiceHandle *v, float p) {
    ArVoice *vo = voice_of(v); if (!vo) return RES_FAIL;
    Renderer *r = atomic_load(&g_r);
    pthread_mutex_lock(&r->m); vo->pitch = (p > 0.0f) ? p : 1.0f; pthread_mutex_unlock(&r->m);
    return RES_OK;
}
static Result voice_matrix_set(ArVoice *vo,float vol,int src,int dst,int count){
    if(!vo)return RES_FAIL;
    if(src<0||src>=vo->channels||dst<0||dst>=count||!isfinite(vol))die("AudioRenderer invalid voice matrix src=%d dst=%d count=%d gain=%f",src,dst,count,vol);
    Renderer *r=atomic_load(&g_r);pthread_mutex_lock(&r->m);vo->mix[src][dst]=vol;
    pthread_mutex_unlock(&r->m);
    if(r->vol_logged<12){r->vol_logged++;LOG("AUDIO_VOICE_MATRIX src=%d dst=%d gain=%.4f",src,dst,vol);}
    return RES_OK;
}
HLE_FORCE("_ZN2nn5audio17SetVoiceMixVolumeEPNS0_9VoiceTypeEPNS0_10SubMixTypeEfii", "声部矩阵（→ 子混音）", Result, h_ar_V2SmVol,
          const ArVoiceHandle *v, const ArVoiceHandle *sm, float vol, int a, int b) {
    ArSubMix *s=sm_of(sm);if(!s)return RES_FAIL;
    return voice_matrix_set(voice_of(v),vol,a,b,s->channels);
}
HLE_FORCE("_ZN2nn5audio17SetVoiceMixVolumeEPNS0_9VoiceTypeEPNS0_12FinalMixTypeEfii", "声部音量（→ 最终混音）", Result, h_ar_V2FmVol,
          const ArVoiceHandle *v, const ArVoiceHandle *fm, float vol, int a, int b) {
    ArFinalMix *f=fm_of(fm);if(!f)return RES_FAIL;
    return voice_matrix_set(voice_of(v),vol,a,b,f->channels);
}
HLE_FORCE("_ZN2nn5audio18SetSubMixMixVolumeEPNS0_10SubMixTypeEPNS0_12FinalMixTypeEfii", "子混音音量", Result, h_ar_SmVol,
          const ArVoiceHandle *sm, const ArVoiceHandle *fm, float vol, int a, int b) {
    ArSubMix *s = sm_of(sm); if (!s) return RES_FAIL;
    ArFinalMix *f=fm_of(fm);if(!f)return RES_FAIL;
    if(a<0||a>=s->channels||b<0||b>=f->channels||!isfinite(vol))die("AudioRenderer invalid submix matrix src=%d dst=%d gain=%f",a,b,vol);
    Renderer *r=atomic_load(&g_r);pthread_mutex_lock(&r->m);s->mix[a][b]=vol;pthread_mutex_unlock(&r->m);
    LOG("AUDIO_SUBMIX_MATRIX src=%d dst=%d gain=%.4f",a,b,vol);
    return RES_OK;
}
HLE_FORCE("_ZN2nn5audio16AppendWaveBufferEPNS0_9VoiceTypeEPKNS0_10WaveBufferE", "追加待播缓冲（bool）", unsigned, h_ar_Append,
          const ArVoiceHandle *v, const ArWaveBuffer *wb) {
    ArVoice *vo = voice_of(v); if (!vo || !wb) return 0u;
    Renderer *r = atomic_load(&g_r);
    pthread_mutex_lock(&r->m);
    if(!vo->used){pthread_mutex_unlock(&r->m);return 0;}
    unsigned bytes_per=vo->format==1?1:vo->format==2?2:4;
    if(!wb->buffer||wb->startSample<0||wb->endSample<=wb->startSample||
       (uint64_t)wb->endSample>wb->bufferSize/(bytes_per*(unsigned)vo->channels)){
        pthread_mutex_unlock(&r->m);return 0;
    }
    if (vo->head - vo->tail + (vo->cur!=NULL) >= R_RING) { pthread_mutex_unlock(&r->m); return 0u; }
    vo->ring[vo->head & (R_RING - 1)] = *wb;
    vo->head++;
    pthread_mutex_unlock(&r->m);
    if (!r->wb_logged) {
        r->wb_logged = 1;
        LOG("audio::AppendWaveBuffer addr=%p bytes=%llu samples=%d..%d rate=%d channels=%d format=%d loop=%u",
            wb->buffer,(unsigned long long)wb->bufferSize,wb->startSample,wb->endSample,vo->rate,vo->channels,vo->format,wb->loop);
    }
    return 1u;
}
HLE_FORCE("_ZN2nn5audio18GetWaveBufferCountEPKNS0_9VoiceTypeE", "待播缓冲数", int, h_ar_WbCount, const ArVoiceHandle *v) {
    ArVoice *vo = voice_of(v); if (!vo) return 0;
    Renderer *r = atomic_load(&g_r);
    pthread_mutex_lock(&r->m); int n = vo->head - vo->tail + (vo->cur!=NULL); pthread_mutex_unlock(&r->m);
    return n;
}
HLE_FORCE("_ZN2nn5audio25GetVoicePlayedSampleCountEPKNS0_9VoiceTypeE",
          "声部累计已播帧数，停止时清零", uint64_t, h_ar_Played, const ArVoiceHandle *v) {
    ArVoice *vo = voice_of(v); if (!vo) return 0;
    Renderer *r = atomic_load(&g_r);
    pthread_mutex_lock(&r->m); uint64_t n = vo->played; pthread_mutex_unlock(&r->m);
    return n;
}

// ---------- 混响 ----------
HLE_FORCE("_ZN2nn5audio21SetI3dl2ReverbEnabledEPNS0_15I3dl2ReverbTypeEb", "混响旁通與wet DSP開關", Result, h_ar_RevOn, const ArVoiceHandle *h, unsigned on) {
    ArReverb *rv = (h && h->p) ? objmap_get(h->p) : NULL; if (!rv) return RES_FAIL;
    Renderer *r=atomic_load(&g_r);pthread_mutex_lock(&r->m);rv->enabled=on&1u;pthread_mutex_unlock(&r->m);
    LOG("AUDIO_REVERB enabled=%d dry_gain=%.4f wet_tail_implemented=true",rv->enabled,rv->params.dry_gain);return RES_OK;
}
HLE_FORCE("_ZN2nn5audio24SetI3dl2ReverbParametersEPNS0_15I3dl2ReverbTypeEPKNS0_23I3dl2ReverbParameterSetE", "配置I3dl2早期反射、衰減與原音參數", Result, h_ar_RevParam, const ArVoiceHandle *h, const ArReverbParams *p) {
    ArReverb *rv=(h&&h->p)?objmap_get(h->p):NULL;if(!rv||!p)return RES_FAIL;
    if(!ar_rev_params_valid(p))die("AudioRenderer invalid I3dl2 parameters");
    Renderer *r=atomic_load(&g_r);pthread_mutex_lock(&r->m);rv->params=*p;ar_rev_configure(rv->dsp,p);pthread_mutex_unlock(&r->m);
    LOG("AUDIO_REVERB_PARAMS dry_gain=%.4f room=%.2f decay_time=%.3f wet_tail_implemented=true",p->dry_gain,p->room,p->decay_time);return RES_OK;
}
// LoadI3dl2ReverbPreset remains in the original SDK: it is a pure 0x30-byte
// parameter initializer. The old override wrote 0x40 zero bytes and corrupted
// adjacent guest data as well as replacing the game's actual preset.
HLE_FORCE("_ZN2nn5audio25SetI3dl2ReverbInputOutputEPNS0_15I3dl2ReverbTypeEPKaS4_i", "混响输入输出映射", Result, h_ar_RevIo, const ArVoiceHandle *h, const int8_t *a, const int8_t *b, int n) {
    ArReverb *rv=(h&&h->p)?objmap_get(h->p):NULL;if(!rv||!a||!b||n<1||n>2)return RES_FAIL;
    ArSubMix *sm=rv->submix;
    for(int c=0;c<n;c++)if(a[c]<0||a[c]>=sm->channels||b[c]<0||b[c]>=sm->channels)die("AudioRenderer invalid reverb bus");
    Renderer *r=atomic_load(&g_r);pthread_mutex_lock(&r->m);rv->count=n;memcpy(rv->input,a,n);memcpy(rv->output,b,n);pthread_mutex_unlock(&r->m);
    LOG("AUDIO_REVERB_IO count=%d input=%d,%d output=%d,%d",n,a[0],n>1?a[1]:-1,b[0],n>1?b[1]:-1);return RES_OK;
}

// ---------- 设备 ----------
HLE_FORCE("_ZN2nn5audio19ListAudioDeviceNameEPNS0_15AudioDeviceNameEi", "设备名列表；宿主 1 个（AudioDeviceName = 0x100 字节）", int, h_ar_DevList, char *out, int n) {
    if (n < 1 || !out) return 0;
    memset(out, 0, 0x100); strcpy(out, "DeviceOut"); return 1;
}
HLE_FORCE("_ZN2nn5audio26SetAudioDeviceOutputVolumeEPKNS0_15AudioDeviceNameEf", "设备输出音量；按百分比记进设备端", Result, h_ar_DevVol, const char *name, float v) {
    (void)name;
    Renderer *r = atomic_load(&g_r); if (!r) return RES_OK;
    int pct = (int)lroundf(v * 100.0f); if (pct < 0) pct = 0; if (pct > 200) pct = 200;
    pthread_mutex_lock(&r->m);g_device_vol_pct=pct;pthread_mutex_unlock(&r->m);
    LOG("audio::SetAudioDeviceOutputVolume %.3f → %d%%", v, pct);
    return RES_OK;
}
