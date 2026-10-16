#include "../android/nvn_platform_clock.h"
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
static uint64_t read_clock(clockid_t id) {
    struct timespec t;if(clock_gettime(id,&t))abort();
    return (uint64_t)t.tv_sec*1000000000ull+(uint64_t)t.tv_nsec;
}
extern uint64_t now_ns(void);
/* NVN AcquireTexture supplies deadlines from the host's mach_absolute_time
 * clock. CLOCK_MONOTONIC is a separate Darwin clock domain: even a constant
 * offset makes every short frame wait sleep for that offset again. */
uint64_t dm_android_nvn_clock_ns(void) {return now_ns();}
uint64_t dm_android_nvn_thread_cpu_ns(void) {return read_clock(CLOCK_THREAD_CPUTIME_ID);}
void dm_android_nvn_schedule_thread(const char *role) {
    pthread_setname_np(role);
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0);
}
void dm_android_nvn_sleep_until(uint64_t deadline) {
    for(;;){uint64_t now=dm_android_nvn_clock_ns();if(now>=deadline)return;
        uint64_t delta=deadline-now;struct timespec t={(time_t)(delta/1000000000ull),(long)(delta%1000000000ull)};
        if(!nanosleep(&t,NULL))return;if(errno!=EINTR)abort();}
}
/* Android performance hint sessions have no Apple equivalent. These hooks only
 * account CPU work and never change guest-visible completion or fence order. */
void dm_android_nvn_work_begin(void) {}
void dm_android_nvn_work_check_thread(void) {}
void dm_android_nvn_work_end(uint64_t frame) {(void)frame;}
void dm_android_nvn_work_reset(int foreground) {(void)foreground;}
uint64_t dm_android_nvn_work_wait_begin(enum dm_android_nvn_wait_kind kind) {(void)kind;return dm_android_nvn_clock_ns();}
void dm_android_nvn_work_wait_end(uint64_t token) {(void)token;}
