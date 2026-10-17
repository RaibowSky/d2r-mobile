#include "../dm/gfx/nvn/nvn_vk_texture_snapshot.h"
#include "../dm/gfx/tex/nvn_bcdec.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

/* Two BC1 blocks inside one simulated 16 KiB host page. The tail can be a
 * different allocation: dirty-page tracking legitimately flags both. */
static unsigned char page[16384], baseline[16], gpu[128];
static unsigned decodes;
static void upload_if_changed(void) {
    if(nvn_vk_texture_snapshot_same(baseline,sizeof baseline,page+512,16))return;
    assert(!nvn_bc_decode(0x41,page+512,16,8,4,gpu,32));
    memcpy(baseline,page+512,16);decodes++;
}
int main(void) {
    const unsigned char blocks[16]={0x00,0xf8,0xe0,0x07,0,0,0,0,0x00,0xf8,0xe0,0x07,0,0,0,0};
    unsigned char reference[128];unsigned old_decodes=0;
    memcpy(page+512,blocks,sizeof blocks);
    /* Force the first upload; an absent baseline must never be a cache hit. */
    assert(!nvn_vk_texture_snapshot_same(NULL,16,page+512,16));
    upload_if_changed();assert(decodes==1&&gpu[0]==255&&gpu[2]==0);
    for(unsigned i=0;i<1000;i++){page[4096]=(unsigned char)i;upload_if_changed();}
    assert(decodes==1); /* no full decode for writes elsewhere on this page */
    /* The original CPU-owned upload path decoded on each candidate write. */
    for(unsigned i=0;i<1001;i++){
        assert(!nvn_bc_decode(0x41,page+512,16,8,4,reference,32));old_decodes++;
    }
    assert(old_decodes==1001&&!memcmp(gpu,reference,sizeof gpu));
    /* A GPU-owned change must survive an unchanged CPU backing. */
    memset(gpu,0xA5,sizeof gpu);page[4096]++;upload_if_changed();
    assert(decodes==1&&gpu[0]==0xA5&&gpu[127]==0xA5);
    /* Actual CPU streaming changes the first block from red to blue. */
    page[512]=0x1f;page[513]=0;upload_if_changed();
    assert(decodes==2&&gpu[0]==0&&gpu[2]==255);
    /* A change only in the last encoded byte must also invalidate. */
    page[527]^=0x55;assert(!nvn_vk_texture_snapshot_same(baseline,16,page+512,16));
    upload_if_changed();assert(decodes==3);
    assert(!nvn_vk_texture_snapshot_same(baseline,15,page+512,16));
    assert(!nvn_vk_texture_snapshot_same(baseline,16,page+512,15));
    assert(!nvn_vk_texture_snapshot_same(baseline,0,page+512,0));
    puts("PASS texture snapshot: 1000 false page updates, GPU-owned pixels preserved, real BC1 streaming, last-byte and extent changes");
    puts("PASS original versus fixed: 1001 versus 1 BC decodes, identical pixels before actual streaming");
    return 0;
}
