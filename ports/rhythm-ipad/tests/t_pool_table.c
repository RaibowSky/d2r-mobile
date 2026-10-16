#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static int fail_allocation;
static void *test_realloc(void *p,size_t bytes) {
    if(fail_allocation){errno=ENOMEM;return NULL;}
    return realloc(p,bytes);
}
#define realloc test_realloc
#include "../dm/gfx/nvn/nvn_vk_pool_table.h"
#undef realloc

typedef struct {uint64_t serial,address,bytes,buffer_handle;} Entry;
static Entry value(size_t i) {return (Entry){i+1,UINT64_C(0x7000000000)+i*0x4000,0x4000,i*19+3};}
int main(void) {
    Entry *entries=NULL;size_t capacity=0,count=0;
    /* Cross both the observed 512 limit and the dirty tracker capacity. */
    for(size_t i=0;i<10000;i++) {
        void *next=NULL;size_t grown=0;
        assert(nvn_vk_pool_table_reserve(entries,capacity,count+1,sizeof(*entries),&next,&grown));
        entries=next;capacity=grown;entries[count++]=value(i);
        if(i==512 || i==1024 || i==4096 || i==9999)
            for(size_t j=0;j<count;j++){Entry expected=value(j);assert(!memcmp(&entries[j],&expected,sizeof expected));}
    }
    assert(capacity>=10000);
    /* The backend removes by swapping the last live entry into the gap. */
    size_t allocated=capacity;
    for(unsigned cycle=0;cycle<20;cycle++) {
        while(count>200)entries[37]=entries[--count];
        while(count<10000){entries[count]=value(count);count++;}
        void *next=NULL;size_t grown=0;
        assert(nvn_vk_pool_table_reserve(entries,capacity,count,sizeof(*entries),&next,&grown));
        assert(next==entries && grown==allocated);
    }
    Entry first=entries[0];void *next=entries;size_t grown=capacity;
    fail_allocation=1;
    assert(!nvn_vk_pool_table_reserve(entries,capacity,capacity+1,sizeof(*entries),&next,&grown));
    assert(errno==ENOMEM && next==entries && grown==capacity && !memcmp(&first,entries,sizeof first));
    fail_allocation=0;
    assert(!nvn_vk_pool_table_reserve(entries,capacity,SIZE_MAX,sizeof(*entries),&next,&grown));
    assert(errno==EOVERFLOW && next==entries && grown==capacity);
    free(entries);
    puts("iPad pool registry: PASS 10000 entries, preserved records, reuse, allocation failure, overflow");
    return 0;
}
