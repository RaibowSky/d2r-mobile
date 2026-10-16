/* Deterministically park Surface suspend after loading g_r but before locking
 * r->m, then close on the guest thread. Close must wait before freeing r. */
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include <errno.h>
static pthread_mutex_t *park_mutex,*lifetime_mutex;
static _Atomic int park_once,parked,release_park,closing,at_close_boundary,closed;
static int observed_lock(pthread_mutex_t *m);
#define pthread_mutex_lock observed_lock
#include "../../host/hle_audio_renderer.c"
#undef pthread_mutex_lock
static int observed_lock(pthread_mutex_t *m){
    if(m==park_mutex&&atomic_exchange(&park_once,0)){
        atomic_store(&parked,1);while(!atomic_load(&release_park))usleep(1000);
    }
    if(m==lifetime_mutex&&atomic_load(&closing))atomic_store(&at_close_boundary,1);
    return pthread_mutex_lock(m);
}
void *objmap_get(const void *key){return (void *)key;}
static void *suspender(void *p){(void)p;dm_audio_renderer_suspend(1);return NULL;}
static void *closer(void *p){atomic_store(&closing,1);h_ar_Close((ArHandle){p,p});atomic_store(&closed,1);return NULL;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x);exit(1);}}while(0)
static int wait_flag(_Atomic int *p){for(int i=0;i<2000;i++){if(atomic_load(p))return 1;usleep(1000);}return 0;}
int main(void){
    lifetime_mutex=&g_ar_lifetime;
    for(int pass=0;pass<16;pass++){
        Renderer *r=calloc(1,sizeof *r);CHECK(r);CHECK(!pthread_mutex_init(&r->m,NULL));atomic_store(&g_r,r);
        park_mutex=&r->m;atomic_store(&park_once,1);atomic_store(&parked,0);atomic_store(&release_park,0);
        atomic_store(&closing,0);atomic_store(&at_close_boundary,0);atomic_store(&closed,0);
        pthread_t a,b;CHECK(!pthread_create(&a,NULL,suspender,NULL));CHECK(wait_flag(&parked));
        CHECK(!pthread_create(&b,NULL,closer,r));CHECK(wait_flag(&at_close_boundary));
        CHECK(!atomic_load(&closed));CHECK(pthread_mutex_trylock(&g_ar_lifetime)==EBUSY);
        atomic_store(&release_park,1);CHECK(!pthread_join(a,NULL));CHECK(!pthread_join(b,NULL));
        CHECK(atomic_load(&closed));CHECK(atomic_load(&g_r)==&g_r_inert);
        park_mutex=NULL;dm_audio_renderer_suspend(0);dm_audio_renderer_suspend(1);
    }
    dm_audio_renderer_suspend(0);
    puts("PASS renderer lifetime: 16 forced suspend/Close interleavings; Close waits before mutex destruction/free; post-Close suspend uses inert renderer");return 0;
}
