#pragma once
#include "nvn_obj.h"

typedef struct {
    NVNdevice *dev;
    NVNmemoryPool *pool;
    ptrdiff_t off;
    size_t size;
} buf_builder_t;
FITS(buf_builder_t, NVNbufferBuilder);

typedef struct {
    uint32_t magic;
    NVNmemoryPool *pool;
    ptrdiff_t off;
    size_t size;
    uint64_t serial;
} buf_t;
FITS(buf_t, NVNbuffer);

static inline void nvn_buf_defaults(buf_builder_t *b) {
    /* SetDefaults preserves the device selected by SetDevice. */
    b->pool = NULL; b->off = 0; b->size = 0;
}
static inline int nvn_buf_init(buf_t *o, const buf_builder_t *b, uint64_t serial) {
    memset(o, 0, sizeof *o);
    const pool_t *p = (const pool_t *)b->pool;
    if (!p || p->magic != MAGIC('P','O','O','L') || !p->mem || b->off < 0 ||
        (uint64_t)b->off > p->size || b->size > p->size - (uint64_t)b->off)
        return 0;
    *o = (buf_t){.magic=MAGIC('B','U','F',0), .pool=b->pool,
                 .off=b->off, .size=b->size, .serial=serial};
    return 1;
}
static inline void *nvn_buf_map(const buf_t *o) {
    if (o->magic != MAGIC('B','U','F',0)) return NULL;
    return ((const pool_t *)o->pool)->mem + o->off;
}
static inline int nvn_buf_range(const buf_t *o, ptrdiff_t off, size_t size) {
    return o->magic == MAGIC('B','U','F',0) && off >= 0 &&
           (uint64_t)off <= o->size && size <= o->size - (uint64_t)off;
}
