/* Deliberately offset the OS clock from the guest host clock. A pacing
 * deadline must stay in the host clock domain, including after EINTR. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static uint64_t host_now = UINT64_C(900000000000);
static uint64_t last_sleep;
static int sleeps, interrupts;
uint64_t now_ns(void) {return host_now;}
static int fake_clock_gettime(clockid_t id, struct timespec *out) {
    (void)id;out->tv_sec=100;out->tv_nsec=0;return 0;
}
static int fake_nanosleep(const struct timespec *duration,struct timespec *remaining) {
    (void)remaining;
    last_sleep=(uint64_t)duration->tv_sec*1000000000ull+(uint64_t)duration->tv_nsec;
    assert(last_sleep<=16666667);
    sleeps++;
    if(interrupts){interrupts--;host_now+=1000000;errno=EINTR;return -1;}
    host_now+=last_sleep;return 0;
}
static int fake_name(const char *name) {(void)name;return 0;}
static int fake_qos(unsigned qos,int priority) {(void)qos;(void)priority;return 0;}
#define clock_gettime fake_clock_gettime
#define nanosleep fake_nanosleep
#define pthread_setname_np fake_name
#define pthread_set_qos_class_self_np fake_qos
#ifndef QOS_CLASS_USER_INTERACTIVE
#define QOS_CLASS_USER_INTERACTIVE 1
#endif
#include "../dm/ipad/clock.c"

int main(void) {
    assert(dm_android_nvn_clock_ns()==host_now);
    uint64_t deadline=host_now+16666667;
    dm_android_nvn_sleep_until(deadline);
    assert(host_now==deadline && sleeps==1 && last_sleep==16666667);
    dm_android_nvn_sleep_until(deadline);
    dm_android_nvn_sleep_until(deadline-1);
    assert(sleeps==1);
    interrupts=1;deadline=host_now+16666667;
    dm_android_nvn_sleep_until(deadline);
    assert(host_now==deadline && sleeps==3 && last_sleep==15666667);
    assert(dm_android_nvn_thread_cpu_ns()==UINT64_C(100000000000));
    puts("iPad pacing clock domain: PASS");
    return 0;
}
