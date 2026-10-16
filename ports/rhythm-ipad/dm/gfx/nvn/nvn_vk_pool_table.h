#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>

/* Grow host bookkeeping only. GPU buffers stay lazy and the caller holds its
 * registry mutex; no entry pointer may survive a registry mutation. Failure
 * leaves both the existing allocation and the caller's outputs unchanged. */
static int nvn_vk_pool_table_reserve(void *current,size_t capacity,size_t required,
                                     size_t entry_size,void **next,size_t *next_capacity) {
    if(!entry_size || capacity>SIZE_MAX/entry_size || required>SIZE_MAX/entry_size) {
        errno=EOVERFLOW;return 0;
    }
    if(required<=capacity){*next=current;*next_capacity=capacity;return 1;}
    size_t limit=SIZE_MAX/entry_size;
    size_t grown=capacity?capacity:512;
    if(grown>limit)grown=limit;
    while(grown<required)grown=grown>limit/2?limit:grown*2;
    void *allocation=realloc(current,grown*entry_size);
    if(!allocation)return 0;
    *next=allocation;*next_capacity=grown;return 1;
}
