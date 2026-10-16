#pragma once
#include <stdint.h>

/* Protect only complete host pages contained in a guest RELRO range.
 * Expanding outward would revoke writes from neighbouring guest data.
 * The two partial edge pages retain their existing host permissions; the
 * exact guest range is still tracked by the PermissionLocked SVC metadata. */
static inline int dm_relro_host_pages(uint64_t address,uint64_t size,uint64_t page,
                                      uint64_t *base,uint64_t *length) {
    if(!base||!length||page<0x1000||(page&(page-1))||!size||
       (address&0xfff)||(size&0xfff)||address>UINT64_MAX-size)return 0;
    uint64_t end=address+size;
    uint64_t first=address&~(page-1);
    if(first!=address){
        if(first>UINT64_MAX-page)return 0;
        first+=page;
    }
    uint64_t last=end&~(page-1);
    *base=first;*length=last>first?last-first:0;
    return 1;
}
