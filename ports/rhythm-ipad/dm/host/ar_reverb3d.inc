/* I3dl2 mono/stereo DSP adapted from Ryujinx (MIT).
 * Copyright (c) Ryujinx Team and Contributors. See
 * ../third_party/ryujinx-audio/LICENSE.txt and SOURCE.md for provenance.
 * Uses float/libm, like that reference; not Nintendo's fixed-point/LUT DSP.
 * All storage is allocated on AddEffect, never in the device callback.
 */
typedef struct {float *data;uint32_t max,delay,read,write;} ArRevDelay;
typedef struct {
    ArRevDelay pre,fdn[4],decay1[4],decay2[4];
    float dry,early_gain,late_gain,pre_previous,pre_target,pre_value;
    float direct[4],current[4],output[4],feedback[4],diffusion;
    uint32_t early[20],reflection,rate;
    uint64_t processed,wet_nonzero;
} ArRevDsp;
static const float ar_rev_fdn_min[4]={5,6,13,14};
static const float ar_rev_fdn_max[4]={45.704f,82.782f,149.94f,271.58f};
static const float ar_rev_d1[4]={17,13,9,7},ar_rev_d2[4]={19,11,10,6};
static const float ar_rev_early_time[20]={.017136f,.059154f,.16173f,.39019f,.42526f,.45541f,.68974f,.74591f,.83384f,.8595f,0,.075024f,.16879f,.2999f,.33744f,.3719f,.59901f,.71674f,.81786f,.85166f};
static const float ar_rev_early_gain[20]={.67096f,.61027f,1,.35680f,.68361f,.65978f,.51939f,.24712f,.45945f,.45021f,.64196f,.54879f,.92925f,.38270f,.72867f,.69794f,.5464f,.24563f,.45214f,.44042f};
static const uint8_t ar_rev_early_stereo[20]={0,0,0,1,1,1,1,0,0,0,1,1,1,0,0,0,0,1,1,1};
static uint32_t ar_rev_samples(uint32_t rate,float ms){return (uint32_t)nearbyintf((rate/1000)*ms);}
static float ar_rev_pow10(float x){return x>=0?1:x<=-5.3f?0:powf(10,x);}
static float ar_rev_cos(float x){return cosf(x*0.017453292519943295f);}
static float ar_rev_sin(float x){return sinf(x*0.017453292519943295f);}
static int ar_rev_delay_init(ArRevDelay *d,uint32_t rate,float ms){
    d->max=d->delay=ar_rev_samples(rate,ms);
    d->data=calloc((size_t)d->max+1,sizeof(float));
    d->write=d->delay;return d->data!=NULL;
}
static void ar_rev_delay_set(ArRevDelay *d,uint32_t rate,float ms){
    uint32_t n=ar_rev_samples(rate,ms);if(n>d->max)n=d->max;
    d->delay=n;d->write=(d->read+n)%(d->max+1);
}
static float ar_rev_read(ArRevDelay *d){return d->data[d->read];}
static float ar_rev_delay_update(ArRevDelay *d,float x){
    d->data[d->write++]=x;float y=ar_rev_read(d);
    if(++d->read>=d->max)d->read=0;
    if(d->write>=d->max)d->write=0;
    return y;
}
static float ar_rev_tap(ArRevDelay *d,uint32_t n){
    int i=(int)d->write-(int)n-1;if(i<0)i+=(int)d->max+1;
    return d->data[i];
}
static float ar_rev_allpass(ArRevDelay *d,float x,float gain){
    float v=x-gain*ar_rev_read(d);return ar_rev_delay_update(d,v)+gain*v;
}
static void ar_rev_free(ArRevDsp *d){
    if(!d)return;free(d->pre.data);
    for(int i=0;i<4;i++){free(d->fdn[i].data);free(d->decay1[i].data);free(d->decay2[i].data);}free(d);
}
static ArRevDsp *ar_rev_create(uint32_t rate){
    if(rate!=48000)return NULL;
    ArRevDsp *d=calloc(1,sizeof *d);if(!d)return NULL;d->rate=rate;
    int ok=ar_rev_delay_init(&d->pre,rate,400);
    for(int i=0;i<4;i++){
        ok&=ar_rev_delay_init(&d->fdn[i],rate,ar_rev_fdn_max[i]);
        ok&=ar_rev_delay_init(&d->decay1[i],rate,ar_rev_d1[i]);
        ok&=ar_rev_delay_init(&d->decay2[i],rate,ar_rev_d2[i]);
    }
    if(!ok){ar_rev_free(d);return NULL;}return d;
}
static int ar_rev_params_valid(const ArReverbParams *p){
    float v[12];memcpy(v,p,sizeof v);for(int i=0;i<12;i++)if(!isfinite(v[i]))return 0;
    return p->room>=-10000&&p->room<=0&&p->room_hf>=-10000&&p->room_hf<=0&&
        p->decay_time>=.1f&&p->decay_time<=20&&p->decay_hf_ratio>=.1f&&p->decay_hf_ratio<=2&&
        p->reflections_gain>=-10000&&p->reflections_gain<=1000&&p->reflections_delay>=0&&p->reflections_delay<=.3f&&
        p->reverb_gain>=-10000&&p->reverb_gain<=2000&&p->reverb_delay>=0&&p->reverb_delay<=.1f&&
        p->diffusion>=0&&p->diffusion<=100&&p->density>=0&&p->density<=100&&
        p->hf_reference>=20&&p->hf_reference<=20000&&p->dry_gain>=0&&p->dry_gain<=1;
}
static void ar_rev_configure(ArRevDsp *d,const ArReverbParams *p){
    d->dry=p->dry_gain;memset(d->feedback,0,sizeof d->feedback);d->pre_value=0;
    d->early_gain=ar_rev_pow10(fminf(p->room+p->reflections_gain,5000)/2000);
    d->late_gain=ar_rev_pow10(fminf(p->room+p->reverb_gain,5000)/2000);
    float hf=ar_rev_pow10(p->room_hf/2000);
    if(hf<1){
        float a=1-hf,b=2-2*hf*ar_rev_cos(256*p->hf_reference/d->rate);
        d->pre_previous=(b-sqrtf(fmaxf(0,b*b-4*a*a)))/(2*a);d->pre_target=1-d->pre_previous;
    }else {d->pre_previous=0;d->pre_target=1;}
    d->reflection=ar_rev_samples(d->rate,1000*(p->reflections_delay+p->reverb_delay));
    d->diffusion=.6f*p->diffusion*.01f;
    for(int i=0;i<4;i++){
        ar_rev_delay_set(&d->fdn[i],d->rate,ar_rev_fdn_min[i]+p->density/100*(ar_rev_fdn_max[i]-ar_rev_fdn_min[i]));
        uint32_t samples=d->fdn[i].delay+d->decay1[i].delay+d->decay2[i].delay;
        float a=-60.0f*samples/(p->decay_time*d->rate),b=a/p->decay_hf_ratio;
        float c=ar_rev_cos(64*p->hf_reference/d->rate)/ar_rev_sin(64*p->hf_reference/d->rate);
        float v=ar_rev_pow10((b-a)/40),e=ar_rev_pow10((b+a)/40)*.7071f;
        d->direct[i]=e*(v*c+1)/(c+v);d->current[i]=e*(1-v*c)/(c+v);d->output[i]=(c-v)/(c+v);
    }
    for(int i=0;i<20;i++){
        uint32_t n=ar_rev_samples(d->rate,p->reflections_delay*1000+ar_rev_early_time[i]*1000*(p->reverb_delay*.9998f+.02f));
        d->early[i]=n<d->pre.max?n:d->pre.max;
    }
}
static void ar_rev_frame(ArRevDsp *d,const float input[2],float output[2],int channels){
    float early[2]={0},feedback_out[4],feedback[4],values[4];
    float tap=ar_rev_tap(&d->pre,d->reflection);
    for(int i=0;i<20;i++)early[channels==1?0:ar_rev_early_stereo[i]]+=ar_rev_tap(&d->pre,d->early[i])*ar_rev_early_gain[i];
    float in=input[0]+(channels==2?input[1]:0);
    d->pre_value=in*d->pre_target+d->pre_value*d->pre_previous;ar_rev_delay_update(&d->pre,d->pre_value);
    for(int i=0;i<4;i++){
        float v=ar_rev_read(&d->fdn[i]);feedback_out[i]=v*d->direct[i]+d->feedback[i];
        d->feedback[i]=v*d->current[i]+feedback_out[i]*d->output[i];
    }
    feedback[0]=feedback_out[2]+feedback_out[1];feedback[1]=-feedback_out[0]-feedback_out[3];
    feedback[2]=feedback_out[0]-feedback_out[3];feedback[3]=feedback_out[1]-feedback_out[2];
    for(int i=0;i<4;i++){
        float v=ar_rev_allpass(&d->decay1[i],tap*d->late_gain+feedback[i],d->diffusion);
        values[i]=ar_rev_allpass(&d->decay2[i],v,-.9f*d->diffusion);ar_rev_delay_update(&d->fdn[i],values[i]);
    }
    int wet=0;
    for(int c=0;c<channels;c++){
        float v=early[c]*d->early_gain+values[c]+(channels==1?values[1]:0);
        wet|=fabsf(v)>1e-8f;output[c]=v+input[c]*d->dry;
    }
    d->processed++;d->wet_nonzero+=wet;
}
