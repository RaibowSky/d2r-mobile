#include "../nvn/nvn_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
    uint8_t memory[512] = {0}; NVNdevice device;
    pool_t pool = {.magic=MAGIC('P','O','O','L'), .mem=memory, .size=512};
    buf_builder_t b = {.dev=&device, .pool=(NVNmemoryPool *)&pool, .off=16, .size=32};
    nvn_buf_defaults(&b); CHECK(b.dev == &device && !b.pool && !b.off && !b.size);
    buf_t o; CHECK(!nvn_buf_init(&o, &b, 1)); CHECK(!nvn_buf_map(&o));
    b.pool=(NVNmemoryPool *)&pool; b.off=128; b.size=384;
    CHECK(nvn_buf_init(&o,&b,7)); CHECK(o.serial==7 && o.size==384 && o.off==128);
    CHECK(nvn_buf_map(&o)==memory+128);
    ((uint8_t *)nvn_buf_map(&o))[383]=0x5a; CHECK(memory[511]==0x5a);
    CHECK(nvn_buf_range(&o,0,384)); CHECK(nvn_buf_range(&o,384,0));
    CHECK(!nvn_buf_range(&o,-1,1)); CHECK(!nvn_buf_range(&o,384,1));
    CHECK(!nvn_buf_range(&o,1,SIZE_MAX));
    b.off=-1; CHECK(!nvn_buf_init(&o,&b,8));
    b.off=513; b.size=0; CHECK(!nvn_buf_init(&o,&b,8));
    b.off=1; b.size=SIZE_MAX; CHECK(!nvn_buf_init(&o,&b,8));
    b.off=0; b.size=512; CHECK(nvn_buf_init(&o,&b,9));
    o.magic=0; CHECK(!nvn_buf_map(&o)); CHECK(!nvn_buf_range(&o,0,0));
    pool.magic=0; CHECK(!nvn_buf_init(&o,&b,10));
    puts("PASS NVN buffer views: defaults, aliasing, bounds, overflow, lifecycle");
    return 0;
}
