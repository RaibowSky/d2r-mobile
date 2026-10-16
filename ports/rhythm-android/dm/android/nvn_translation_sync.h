#pragma once
#include "../gfx/nvn/nvn_obj.h"
#include "nvn_translation_queue.h"

/* Mark fences before replay can start. Walk only the host-generated record
 * headers, including CALL/JUMP; command payloads/resources remain in guest
 * memory under NVN's normal until-completion lifetime contract. No allocation
 * or Vulkan calls here. Bound malformed/cyclic streams just as replay does. */
static inline int dm_nvn_mark_fences(const uint8_t *p,unsigned depth) {
    if(depth>32)return 0;
    for(unsigned guard=0;p && guard<1000000;guard++) {
        op_hdr h;memcpy(&h,p,sizeof h);
        if(h.size<sizeof h || h.size%8 || h.op>=OP_COUNT_)return 0;
        const uint8_t *a=p+sizeof h;
        if(h.op==OP_END)return 1;
        if(h.op==OP_CALL || h.op==OP_JUMP) {
            if(h.size<16)return 0;
            const uint8_t *next;memcpy(&next,a,sizeof next);
            if(h.op==OP_JUMP){p=next;continue;}
            if(!dm_nvn_mark_fences(next,depth+1))return 0;
        } else if(h.op==OP_FENCE_SYNC) {
            if(h.size<sizeof h+sizeof(p_sync))return 0;
            p_sync fence;memcpy(&fence,a,sizeof fence);
            if(!fence.sync || fence.sync->magic!=MAGIC('S','Y','N','C'))return 0;
            atomic_fetch_add(&fence.sync->pending,1);
        }
        p+=h.size;
    }
    return 0;
}
static inline void dm_nvn_sync_publish(sync_t *s,uint64_t fence) {
    /* Direct backend probes do not go through the host translation queue. */
    if(!dm_nvn_translation_current_ticket())atomic_fetch_add(&s->pending,1);
    atomic_store(&s->fence,fence);
    atomic_store(&s->fenced,1);
    atomic_fetch_add(&s->translated,1);
}
