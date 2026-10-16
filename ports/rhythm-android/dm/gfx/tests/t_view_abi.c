#include "../nvn/nvn_obj.h"
#include <stdio.h>
#include <stddef.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){
    // Match the game's two adjacent stack views followed by a copy region.
    struct { unsigned char first[16],second[16];NVNcopyRegion region; } frame;
    memset(&frame,0xa5,sizeof frame);
    frame.region=(NVNcopyRegion){0,0,0,1280,720,1};
    view_t *a=(void *)frame.first,*b=(void *)frame.second;
    memset(a,0,sizeof *a);a->set_levels=1;a->base_level=2;a->nlevels=1;
    memset(b,0,sizeof *b);b->set_layers=1;b->min_layer=7;b->nlayers=-1;
    b->set_swizzle=1;b->swizzle_r=5;b->swizzle_g=1;b->swizzle_b=0;b->swizzle_a=2;
    CHECK(sizeof(view_t)==16&&sizeof(NVNtextureView)==16);
    CHECK(a->base_level==2&&a->nlevels==1&&b->min_layer==7&&b->nlayers==-1);
    CHECK(nvn_view_swizzle(b,0)==5&&nvn_view_swizzle(b,1)==1&&nvn_view_swizzle(b,2)==0&&nvn_view_swizzle(b,3)==2);
    CHECK(frame.region.width==1280&&frame.region.height==720&&frame.region.depth==1);
    puts("PASS title view ABI: 16-byte objects preserve adjacent views and copy region");return 0;
}
