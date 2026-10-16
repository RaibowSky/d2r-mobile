#include "../dm/host/relro_pages.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    uint64_t base=0,size=0;
    /* Reproduced RELRO offset from the iPad startup log (no game bytes). */
    assert(dm_relro_host_pages(0xcd9000,0xcc000,0x4000,&base,&size));
    assert(base==0xcdc000&&size==0xc8000);
    assert(base>=0xcd9000&&base+size<=0xda5000);
    assert(dm_relro_host_pages(0x10000,0xc000,0x4000,&base,&size));
    assert(base==0x10000&&size==0xc000);
    assert(dm_relro_host_pages(0x11000,0x1000,0x4000,&base,&size)&&size==0);
    assert(dm_relro_host_pages(0x10000,0x1000,0x4000,&base,&size)&&size==0);
    assert(dm_relro_host_pages(0x11000,0x3000,0x4000,&base,&size)&&size==0);
    assert(dm_relro_host_pages(0x11000,0x9000,0x1000,&base,&size));
    assert(base==0x11000&&size==0x9000);
    assert(!dm_relro_host_pages(0x11001,0x1000,0x4000,&base,&size));
    assert(!dm_relro_host_pages(0x11000,0x1001,0x4000,&base,&size));
    assert(!dm_relro_host_pages(0x11000,0,0x4000,&base,&size));
    assert(!dm_relro_host_pages(UINT64_MAX-0xfff,0x1000,0x4000,&base,&size));
    assert(!dm_relro_host_pages(0x11000,0x1000,0x3000,&base,&size));
    assert(!dm_relro_host_pages(0x11000,0x1000,0,&base,&size));
    assert(!dm_relro_host_pages(0x11000,0x1000,0x4000,0,&size));
    assert(!dm_relro_host_pages(0x11000,0x1000,0x4000,&base,0));
    puts("PASS RELRO pages: shared writable neighbours, 4K/16K and invalid ranges");
    return 0;
}
